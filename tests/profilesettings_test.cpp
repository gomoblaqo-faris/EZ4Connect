#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

#include "application/applicationconstants.h"
#include "application/defaultsettings.h"
#include "application/profilesettings.h"
#include "application/secretstore.h"

#include <QHash>

namespace
{
class FakeSecretStore : public SecretStore
{
public:
    QHash<QString, QString> entries;
    bool writesSucceed = true;
    bool readsSucceed = true;
    bool removesSucceed = true;

    std::optional<QString> read(const QString &account) override
    {
        return readsSucceed && entries.contains(account)
            ? std::optional<QString>(entries.value(account))
            : std::nullopt;
    }

    bool write(const QString &account, const QString &secret) override
    {
        if (!writesSucceed)
        {
            return false;
        }
        entries.insert(account, secret);
        return true;
    }

    bool remove(const QString &account) override
    {
        if (!removesSucceed)
        {
            return false;
        }
        entries.remove(account);
        return true;
    }
};

// Installs a store for one test and always takes it out again.
struct ScopedSecretStore
{
    ScopedSecretStore()
    {
        ProfileSettings::setSecretStore(&store);
    }

    ~ScopedSecretStore()
    {
        ProfileSettings::setSecretStore(nullptr);
    }

    FakeSecretStore store;
};

bool missingKeysUseTheLegacyFallback()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath("legacy.ini"), QSettings::IniFormat);

    const bool passed =
        ProfileSettings::read(settings, ProfileSettings::Protocol) == "easyconnect"
        && ProfileSettings::read(settings, ProfileSettings::AuthType) == "psw"
        && ProfileSettings::read(settings, ProfileSettings::ServerAddress).isEmpty()
        && ProfileSettings::read(settings, ProfileSettings::ServerPort) == 443
        && ProfileSettings::read(settings, ProfileSettings::HTTPPort) == 11081
        && !ProfileSettings::read(settings, ProfileSettings::DNSAuto)
        && !ProfileSettings::read(settings, ProfileSettings::CheckUpdateAfterStart)
        && ProfileSettings::read(settings, ProfileSettings::ConfigVersion) == -1
        && ProfileSettings::read(settings, ProfileSettings::Password).isEmpty()
        && !ProfileSettings::contains(settings, ProfileSettings::Protocol);
    if (!passed)
    {
        qCritical() << "missingKeysUseTheLegacyFallback failed";
    }
    return passed;
}

bool newProfilesStartFromTheInitialValues()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath("new.ini"), QSettings::IniFormat);
    DefaultSettings::reset(settings);

    // The stored form is checked as well, because it is what older and
    // newer versions of the app read.
    const bool passed =
        ProfileSettings::read(settings, ProfileSettings::Protocol) == "atrust"
        && ProfileSettings::read(settings, ProfileSettings::AuthType) == "cas"
        && ProfileSettings::read(settings, ProfileSettings::LoginDomain) == "hitcas"
        && ProfileSettings::read(settings, ProfileSettings::DNSAuto)
        && settings.value("ZJUConnect/ServerAddress").toString() == "trust.hitsz.edu.cn"
        && settings.value("ZJUConnect/ServerPort").toInt() == 443
        && settings.value("ZJUConnect/SOCKS5Port").toInt() == 11080
        && settings.value("ZJUConnect/EasyConnectAuthType").toString() == "password"
        && settings.value("Credential/Password").toString().isEmpty()
        && settings.value("Common/ConfigVersion").toInt() == ApplicationConstants::ConfigVersion
        && !settings.contains("Credential/CertFile");
    if (!passed)
    {
        qCritical() << "newProfilesStartFromTheInitialValues failed";
    }
    return passed;
}

bool storedValuesWinOverFallbacks()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath("stored.ini"), QSettings::IniFormat);
    ProfileSettings::write(settings, ProfileSettings::Protocol, "atrust");
    ProfileSettings::write(settings, ProfileSettings::HTTPPort, 8081);
    ProfileSettings::write(settings, ProfileSettings::TUNMode, true);
    settings.sync();

    // Values come back from an INI file as text.
    QSettings reloaded(directory.filePath("stored.ini"), QSettings::IniFormat);
    const bool passed =
        ProfileSettings::read(reloaded, ProfileSettings::Protocol) == "atrust"
        && ProfileSettings::read(reloaded, ProfileSettings::HTTPPort) == 8081
        && ProfileSettings::read(reloaded, ProfileSettings::TUNMode);
    if (!passed)
    {
        qCritical() << "storedValuesWinOverFallbacks failed";
    }
    return passed;
}

bool secretsAreStoredEncodedAndReadBackIntact()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath("secret.ini"), QSettings::IniFormat);
    const QString password = QStringLiteral("p\u00e4ss w\u00f6rd/+=");
    ProfileSettings::write(settings, ProfileSettings::Password, password);

    // The stored form must stay what earlier versions wrote and expect.
    const QString expectedStored = QString::fromLatin1(password.toUtf8().toBase64());
    const bool passed =
        settings.value("Credential/Password").toString() == expectedStored
        && ProfileSettings::read(settings, ProfileSettings::Password) == password;
    if (!passed)
    {
        qCritical() << "secretsAreStoredEncodedAndReadBackIntact failed";
    }
    return passed;
}

bool secretsGoToTheStoreAndLeaveTheFile()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath("stored-secret.ini"), QSettings::IniFormat);
    ScopedSecretStore scoped;

    ProfileSettings::write(settings, ProfileSettings::Password, "hunter2");
    ProfileSettings::write(settings, ProfileSettings::TOTPSecret, "JBSWY3DP");
    const QString secretId = ProfileSettings::read(settings, ProfileSettings::SecretId);

    const bool passed = !secretId.isEmpty()
        && !settings.contains("Credential/Password")
        && !settings.contains("Credential/TOTPSecret")
        && scoped.store.entries.value(secretId + "/Credential/Password") == "hunter2"
        && scoped.store.entries.value(secretId + "/Credential/TOTPSecret") == "JBSWY3DP"
        && ProfileSettings::read(settings, ProfileSettings::Password) == "hunter2"
        && ProfileSettings::read(settings, ProfileSettings::TOTPSecret) == "JBSWY3DP";
    if (!passed)
    {
        qCritical() << "secretsGoToTheStoreAndLeaveTheFile failed";
    }
    return passed;
}

bool clearingASecretRemovesItFromTheStore()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath("cleared-secret.ini"), QSettings::IniFormat);
    ScopedSecretStore scoped;

    ProfileSettings::write(settings, ProfileSettings::Password, "hunter2");
    ProfileSettings::write(settings, ProfileSettings::Password, QString());

    const bool passed = scoped.store.entries.isEmpty()
        && ProfileSettings::read(settings, ProfileSettings::Password).isEmpty();
    if (!passed)
    {
        qCritical() << "clearingASecretRemovesItFromTheStore failed";
    }
    return passed;
}

bool aSecretTheStoreRejectsStaysInTheFile()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath("rejected-secret.ini"), QSettings::IniFormat);
    ScopedSecretStore scoped;
    scoped.store.writesSucceed = false;

    // Losing a saved password would be worse than keeping it where it was.
    ProfileSettings::write(settings, ProfileSettings::Password, "hunter2");

    const bool passed = scoped.store.entries.isEmpty()
        && settings.contains("Credential/Password")
        && ProfileSettings::read(settings, ProfileSettings::Password) == "hunter2";
    if (!passed)
    {
        qCritical() << "aSecretTheStoreRejectsStaysInTheFile failed";
    }
    return passed;
}

bool migratesSecretsWrittenByOlderVersions()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath("legacy-secret.ini"), QSettings::IniFormat);
    // What a version without a credential store leaves in the file.
    ProfileSettings::write(settings, ProfileSettings::Password, "hunter2");
    ProfileSettings::write(settings, ProfileSettings::CertPassword, QString());
    ProfileSettings::write(settings, ProfileSettings::TOTPSecret, "JBSWY3DP");

    ScopedSecretStore scoped;
    if (ProfileSettings::read(settings, ProfileSettings::Password) != "hunter2")
    {
        qCritical() << "a secret still in the file was not readable once a store was present";
        return false;
    }

    ProfileSettings::migrateSecrets(settings);
    const bool passed = !settings.contains("Credential/Password")
        && !settings.contains("Credential/TOTPSecret")
        && scoped.store.entries.size() == 2
        && ProfileSettings::read(settings, ProfileSettings::Password) == "hunter2"
        && ProfileSettings::read(settings, ProfileSettings::TOTPSecret) == "JBSWY3DP"
        && ProfileSettings::read(settings, ProfileSettings::CertPassword).isEmpty();
    if (!passed)
    {
        qCritical() << "migratesSecretsWrittenByOlderVersions failed";
    }
    return passed;
}

bool aCopiedProfileGetsItsOwnSecrets()
{
    QTemporaryDir directory;
    ScopedSecretStore scoped;
    QSettings original(directory.filePath("original.ini"), QSettings::IniFormat);
    ProfileSettings::write(original, ProfileSettings::Password, "hunter2");
    original.sync();

    QFile::copy(directory.filePath("original.ini"), directory.filePath("copy.ini"));
    QSettings copy(directory.filePath("copy.ini"), QSettings::IniFormat);
    ProfileSettings::detachSecrets(copy);
    ProfileSettings::write(copy, ProfileSettings::Password, "changed");

    const bool passed =
        ProfileSettings::read(copy, ProfileSettings::SecretId)
            != ProfileSettings::read(original, ProfileSettings::SecretId)
        && ProfileSettings::read(original, ProfileSettings::Password) == "hunter2"
        && ProfileSettings::read(copy, ProfileSettings::Password) == "changed";
    if (!passed)
    {
        qCritical() << "aCopiedProfileGetsItsOwnSecrets failed";
    }
    return passed;
}

bool forgettingAProfileRemovesItsSecretsOnly()
{
    QTemporaryDir directory;
    ScopedSecretStore scoped;
    QSettings kept(directory.filePath("kept.ini"), QSettings::IniFormat);
    QSettings removed(directory.filePath("removed.ini"), QSettings::IniFormat);
    ProfileSettings::write(kept, ProfileSettings::Password, "keep-me");
    ProfileSettings::write(removed, ProfileSettings::Password, "remove-me");
    ProfileSettings::write(removed, ProfileSettings::TOTPSecret, "remove-me-too");

    ProfileSettings::forgetSecrets(removed);
    const bool passed = scoped.store.entries.size() == 1
        && ProfileSettings::read(kept, ProfileSettings::Password) == "keep-me"
        && !removed.contains("Credential/SecretId");
    if (!passed)
    {
        qCritical() << "forgettingAProfileRemovesItsSecretsOnly failed";
    }
    return passed;
}

bool resettingAProfileForgetsItsSecrets()
{
    QTemporaryDir directory;
    ScopedSecretStore scoped;
    QSettings settings(directory.filePath("reset.ini"), QSettings::IniFormat);
    ProfileSettings::write(settings, ProfileSettings::Password, "hunter2");

    ProfileSettings::forgetSecrets(settings);
    settings.clear();
    DefaultSettings::reset(settings);

    const bool passed = scoped.store.entries.isEmpty()
        && ProfileSettings::read(settings, ProfileSettings::Password).isEmpty();
    if (!passed)
    {
        qCritical() << "resettingAProfileForgetsItsSecrets failed";
    }
    return passed;
}

bool anUnreadableSecretIsNotErasedBySavingAForm()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath("unreadable.ini"), QSettings::IniFormat);
    ScopedSecretStore scoped;
    ProfileSettings::write(settings, ProfileSettings::Password, "hunter2");

    // A locked credential store makes the form load an empty password.
    scoped.store.readsSucceed = false;
    const QString loaded = ProfileSettings::read(settings, ProfileSettings::Password);
    scoped.store.readsSucceed = true;
    ProfileSettings::writeIfChanged(settings, ProfileSettings::Password, loaded, loaded);

    const bool passed = loaded.isEmpty()
        && ProfileSettings::read(settings, ProfileSettings::Password) == "hunter2";
    if (!passed)
    {
        qCritical() << "anUnreadableSecretIsNotErasedBySavingAForm failed";
        return false;
    }

    ProfileSettings::writeIfChanged(settings, ProfileSettings::Password, loaded, "changed");
    if (ProfileSettings::read(settings, ProfileSettings::Password) != "changed")
    {
        qCritical() << "a secret the user changed was not saved";
        return false;
    }
    return true;
}

bool reportsSecretsThatCouldNotBeRemoved()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath("stuck.ini"), QSettings::IniFormat);
    ScopedSecretStore scoped;
    ProfileSettings::write(settings, ProfileSettings::Password, "hunter2");

    scoped.store.removesSucceed = false;
    if (ProfileSettings::forgetSecrets(settings))
    {
        qCritical() << "a failed removal from the store was reported as success";
        return false;
    }
    return true;
}

bool aFailedRemovalIsRememberedAndRetried()
{
    QTemporaryDir directory;
    ScopedSecretStore scoped;
    ProfileSettings::setPendingRemovalsFile(directory.filePath("state.ini"));
    QSettings settings(directory.filePath("stuck.ini"), QSettings::IniFormat);
    ProfileSettings::write(settings, ProfileSettings::Password, "hunter2");
    const QString secretId = ProfileSettings::read(settings, ProfileSettings::SecretId);

    // The profile is deleted while the credential store is unavailable.
    scoped.store.removesSucceed = false;
    ProfileSettings::forgetSecrets(settings);
    ProfileSettings::retryPendingSecretRemovals();
    QSettings state(directory.filePath("state.ini"), QSettings::IniFormat);
    if (!state.value("Secrets/PendingRemoval").toStringList()
             .contains(secretId + "/Credential/Password")
        || scoped.store.entries.size() != 1)
    {
        qCritical() << "a removal that failed was not kept for a later attempt";
        ProfileSettings::setPendingRemovalsFile(QString());
        return false;
    }

    scoped.store.removesSucceed = true;
    ProfileSettings::retryPendingSecretRemovals();
    state.sync();
    const bool passed = scoped.store.entries.isEmpty()
        && !state.contains("Secrets/PendingRemoval");
    ProfileSettings::setPendingRemovalsFile(QString());
    if (!passed)
    {
        qCritical() << "a remembered removal was not completed on the next attempt";
    }
    return passed;
}

bool aSecretSavedAgainIsNotRemovedByAnOlderRequest()
{
    QTemporaryDir directory;
    ScopedSecretStore scoped;
    ProfileSettings::setPendingRemovalsFile(directory.filePath("state.ini"));
    QSettings settings(directory.filePath("profile.ini"), QSettings::IniFormat);
    ProfileSettings::write(settings, ProfileSettings::Password, "first");
    ProfileSettings::write(settings, ProfileSettings::TOTPSecret, "JBSWY3DP");

    // The password is cleared while the credential store is unavailable...
    scoped.store.removesSucceed = false;
    ProfileSettings::write(settings, ProfileSettings::Password, QString());
    QSettings state(directory.filePath("state.ini"), QSettings::IniFormat);
    const bool remembered = state.value("Secrets/PendingRemoval").toStringList().size() == 1;

    // ...and a new one is saved before the removal could be retried.
    scoped.store.removesSucceed = true;
    ProfileSettings::write(settings, ProfileSettings::Password, "second");
    ProfileSettings::retryPendingSecretRemovals();

    const bool passed = remembered
        && ProfileSettings::read(settings, ProfileSettings::Password) == "second"
        // The profile's other secret was never part of the request.
        && ProfileSettings::read(settings, ProfileSettings::TOTPSecret) == "JBSWY3DP";
    ProfileSettings::setPendingRemovalsFile(QString());
    if (!passed)
    {
        qCritical() << "aSecretSavedAgainIsNotRemovedByAnOlderRequest failed";
    }
    return passed;
}

bool anExportedCopyDropsCredentialsInFreeFormSettings()
{
    QTemporaryDir directory;
    QSettings harmless(directory.filePath("harmless.ini"), QSettings::IniFormat);
    ProfileSettings::write(harmless, ProfileSettings::ExtraArguments, "-foo bar");
    ProfileSettings::write(harmless, ProfileSettings::LoginURL, "/passport/v1/public/casLogin?sfDomain=hitcas");
    ProfileSettings::stripSecrets(harmless);

    QSettings risky(directory.filePath("risky.ini"), QSettings::IniFormat);
    ProfileSettings::write(risky, ProfileSettings::ExtraArguments, "-foo bar -password hunter2");
    ProfileSettings::write(risky, ProfileSettings::LoginURL, "https://sso.example.edu/login?ticket=ST-12345");
    ProfileSettings::stripSecrets(risky);

    const bool passed =
        ProfileSettings::read(harmless, ProfileSettings::ExtraArguments) == "-foo bar"
        && ProfileSettings::read(harmless, ProfileSettings::LoginURL)
            == "/passport/v1/public/casLogin?sfDomain=hitcas"
        && ProfileSettings::read(risky, ProfileSettings::ExtraArguments).isEmpty()
        && ProfileSettings::read(risky, ProfileSettings::LoginURL) == "https://sso.example.edu/login";
    if (!passed)
    {
        qCritical() << "anExportedCopyDropsCredentialsInFreeFormSettings failed";
    }
    return passed;
}

bool anExportedCopyCarriesNoSecretsOrStoreIdentifier()
{
    QTemporaryDir directory;
    ScopedSecretStore scoped;
    QSettings settings(directory.filePath("profile.ini"), QSettings::IniFormat);
    ProfileSettings::write(settings, ProfileSettings::Username, "alice");
    ProfileSettings::write(settings, ProfileSettings::Password, "hunter2");
    // A secret the store refused stays in the file and must not be exported.
    scoped.store.writesSucceed = false;
    ProfileSettings::write(settings, ProfileSettings::TOTPSecret, "JBSWY3DP");
    ProfileSettings::write(settings, ProfileSettings::ShadowsocksURL, "ss://aes:hunter3@example.org:8388");
    ProfileSettings::write(settings, ProfileSettings::DialDirectProxy, "socks://bob:hunter4@10.0.0.9:1080");
    settings.sync();

    QFile::copy(directory.filePath("profile.ini"), directory.filePath("exported.ini"));
    QSettings exported(directory.filePath("exported.ini"), QSettings::IniFormat);
    ProfileSettings::stripSecrets(exported);
    exported.sync();

    QFile file(directory.filePath("exported.ini"));
    const QByteArray content = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("unreadable");
    const bool passed = content.contains("alice")
        && !content.contains("JBSWY3DP")
        && !content.contains("hunter3")
        && !content.contains("hunter4")
        && !content.contains("SecretId")
        && !content.contains("Password")
        && ProfileSettings::read(settings, ProfileSettings::Password) == "hunter2"
        && ProfileSettings::read(settings, ProfileSettings::TOTPSecret) == "JBSWY3DP";
    if (!passed)
    {
        qCritical() << "anExportedCopyCarriesNoSecretsOrStoreIdentifier failed";
    }
    return passed;
}

bool infersEasyConnectAuthenticationForOlderProfiles()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath("auth.ini"), QSettings::IniFormat);
    if (ProfileSettings::easyConnectAuthType(settings) != "password")
    {
        qCritical() << "a profile without a certificate was not treated as password authentication";
        return false;
    }

    ProfileSettings::write(settings, ProfileSettings::CertFile, "/tmp/client.p12");
    if (ProfileSettings::easyConnectAuthType(settings) != "certificate")
    {
        qCritical() << "a profile with a certificate was not treated as certificate authentication";
        return false;
    }

    ProfileSettings::write(settings, ProfileSettings::EasyConnectAuthType, "password");
    if (ProfileSettings::easyConnectAuthType(settings) != "password")
    {
        qCritical() << "the stored authentication type did not win over the inferred one";
        return false;
    }
    return true;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    return missingKeysUseTheLegacyFallback()
        && newProfilesStartFromTheInitialValues()
        && storedValuesWinOverFallbacks()
        && secretsAreStoredEncodedAndReadBackIntact()
        && secretsGoToTheStoreAndLeaveTheFile()
        && clearingASecretRemovesItFromTheStore()
        && aSecretTheStoreRejectsStaysInTheFile()
        && migratesSecretsWrittenByOlderVersions()
        && aCopiedProfileGetsItsOwnSecrets()
        && forgettingAProfileRemovesItsSecretsOnly()
        && resettingAProfileForgetsItsSecrets()
        && anUnreadableSecretIsNotErasedBySavingAForm()
        && reportsSecretsThatCouldNotBeRemoved()
        && aFailedRemovalIsRememberedAndRetried()
        && aSecretSavedAgainIsNotRemovedByAnOlderRequest()
        && anExportedCopyDropsCredentialsInFreeFormSettings()
        && anExportedCopyCarriesNoSecretsOrStoreIdentifier()
        && infersEasyConnectAuthenticationForOlderProfiles() ? 0 : 1;
}
