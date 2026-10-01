#include <QCoreApplication>
#include <QDebug>
#include <QSettings>
#include <QTemporaryDir>

#include "application/applicationconstants.h"
#include "application/defaultsettings.h"
#include "application/profilesettings.h"

namespace
{
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
        && infersEasyConnectAuthenticationForOlderProfiles() ? 0 : 1;
}
