#include "settingsprofileloader.h"

#include "application/profilesettings.h"

ConnectionProfile SettingsProfileLoader::load(
    const QSettings &settings,
    const QString &profileId,
    const QString &username,
    const QString &password
)
{
    ConnectionProfile profile;
    profile.profileId = profileId;
    const QString easyconnectAuthType = ProfileSettings::easyConnectAuthType(settings);
    const bool useCertificate =
        ProfileSettings::read(settings, ProfileSettings::Protocol) == "easyconnect"
        && easyconnectAuthType == "certificate";
    profile.credentials = {
        username,
        password,
        ProfileSettings::read(settings, ProfileSettings::TOTPSecret),
        useCertificate
            ? ProfileSettings::read(settings, ProfileSettings::CertFile)
            : QString(),
        useCertificate
            ? ProfileSettings::read(settings, ProfileSettings::CertPassword)
            : QString(),
        ProfileSettings::read(settings, ProfileSettings::CredentialsAsArguments)
    };

    const QString countryCode = ProfileSettings::read(settings, ProfileSettings::PhoneCountryCode);
    const QString phoneNumber = ProfileSettings::read(settings, ProfileSettings::PhoneNumber);
    const QString phone = !countryCode.isEmpty() && !phoneNumber.isEmpty()
        ? countryCode + "-" + phoneNumber
        : QString();
    profile.endpoint = {
        ProfileSettings::read(settings, ProfileSettings::Protocol),
        ProfileSettings::read(settings, ProfileSettings::AuthType),
        ProfileSettings::read(settings, ProfileSettings::LoginDomain),
        phone,
        ProfileSettings::read(settings, ProfileSettings::ServerAddress),
        ProfileSettings::read(settings, ProfileSettings::ServerPort)
    };

    profile.dns = {
        ProfileSettings::read(settings, ProfileSettings::DNS),
        ProfileSettings::read(settings, ProfileSettings::DNSAuto),
        ProfileSettings::read(settings, ProfileSettings::SecondaryDNS),
        ProfileSettings::read(settings, ProfileSettings::DNSTTL),
        ProfileSettings::read(settings, ProfileSettings::DisableZJUDNS),
        ProfileSettings::read(settings, ProfileSettings::CustomDNS),
        ProfileSettings::read(settings, ProfileSettings::LocalDNSServer),
        ProfileSettings::read(settings, ProfileSettings::DNSServerBind)
    };

    const QString bindPrefix = ProfileSettings::read(settings, ProfileSettings::OutsideAccess)
        ? "[::]:"
        : "127.0.0.1:";
    profile.proxy = {
        bindPrefix + QString::number(ProfileSettings::read(settings, ProfileSettings::SOCKS5Port)),
        bindPrefix + QString::number(ProfileSettings::read(settings, ProfileSettings::HTTPPort)),
        ProfileSettings::read(settings, ProfileSettings::ShadowsocksURL),
        ProfileSettings::read(settings, ProfileSettings::DialDirectProxy),
        ProfileSettings::read(settings, ProfileSettings::ProxyAll),
        ProfileSettings::read(settings, ProfileSettings::CustomProxyDomain)
    };

    profile.tunnel = {
        ProfileSettings::read(settings, ProfileSettings::TUNMode),
        ProfileSettings::read(settings, ProfileSettings::AddRoute),
        ProfileSettings::read(settings, ProfileSettings::DNSHijack),
        ProfileSettings::read(settings, ProfileSettings::FakeIP),
        ProfileSettings::read(settings, ProfileSettings::TCPTunnelMode),
        ProfileSettings::read(settings, ProfileSettings::TCPPortForwarding),
        ProfileSettings::read(settings, ProfileSettings::UDPPortForwarding)
    };

    profile.behavior = {
        ProfileSettings::read(settings, ProfileSettings::UpdateBestNodesInterval),
        !ProfileSettings::read(settings, ProfileSettings::MultiLine),
        !ProfileSettings::read(settings, ProfileSettings::KeepAlive),
        ProfileSettings::read(settings, ProfileSettings::KeepAliveURL),
        ProfileSettings::read(settings, ProfileSettings::BindInterface),
        ProfileSettings::read(settings, ProfileSettings::AutoDetectInterface),
        ProfileSettings::read(settings, ProfileSettings::SkipDomainResource),
        ProfileSettings::read(settings, ProfileSettings::DisableServerConfig),
        !ProfileSettings::read(settings, ProfileSettings::ZJUDefault)
    };

    profile.debug = {
        ProfileSettings::read(settings, ProfileSettings::Debug),
        ProfileSettings::read(settings, ProfileSettings::DebugPCAP),
        ProfileSettings::read(settings, ProfileSettings::DebugTLSLog)
    };

    profile.extraArguments = ProfileSettings::read(settings, ProfileSettings::ExtraArguments);
    return profile;
}
