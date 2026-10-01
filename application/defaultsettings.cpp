#include "application/defaultsettings.h"

#include <QSettings>

#include "application/profilesettings.h"

void DefaultSettings::reset(QSettings &settings)
{
    using namespace ProfileSettings;

    writeInitial(settings, Username);
    write(settings, Password, QString());
    write(settings, TOTPSecret, QString());

    writeInitial(settings, ConnectAfterStart);
    writeInitial(settings, CheckUpdateAfterStart);
    writeInitial(settings, AutoSetProxy);
    writeInitial(settings, ReconnectTime);
    writeInitial(settings, AutoReconnect);
    writeInitial(settings, SystemProxyBypass);

    writeInitial(settings, ServerAddress);
    writeInitial(settings, ServerPort);
    writeInitial(settings, DNS);
    writeInitial(settings, DNSAuto);
    writeInitial(settings, SecondaryDNS);
    writeInitial(settings, LocalDNSServer);
    writeInitial(settings, DNSServerBind);
    writeInitial(settings, DNSTTL);
    writeInitial(settings, SOCKS5Port);
    writeInitial(settings, HTTPPort);
    write(settings, ShadowsocksURL, QString());
    writeInitial(settings, DialDirectProxy);
    writeInitial(settings, UpdateBestNodesInterval);
    writeInitial(settings, CredentialsAsArguments);

    writeInitial(settings, Protocol);
    writeInitial(settings, EasyConnectAuthType);
    writeInitial(settings, LoginDomain);
    writeInitial(settings, AuthType);
    writeInitial(settings, LoginURL);
    writeInitial(settings, PhoneCountryCode);
    writeInitial(settings, PhoneNumber);

    writeInitial(settings, MultiLine);
    writeInitial(settings, KeepAlive);
    writeInitial(settings, KeepAliveURL);
    writeInitial(settings, BindInterface);
    writeInitial(settings, OutsideAccess);
    writeInitial(settings, SkipDomainResource);
    writeInitial(settings, DisableServerConfig);
    writeInitial(settings, ProxyAll);
    writeInitial(settings, DisableZJUDNS);
    writeInitial(settings, ZJUDefault);
    writeInitial(settings, Debug);
    writeInitial(settings, DebugPCAP);
    writeInitial(settings, DebugTLSLog);

    writeInitial(settings, TUNMode);
    writeInitial(settings, AddRoute);
    writeInitial(settings, DNSHijack);
    writeInitial(settings, FakeIP);
    writeInitial(settings, TCPTunnelMode);
    writeInitial(settings, AutoDetectInterface);

    writeInitial(settings, TCPPortForwarding);
    writeInitial(settings, UDPPortForwarding);
    writeInitial(settings, CustomDNS);
    writeInitial(settings, CustomProxyDomain);
    writeInitial(settings, ExtraArguments);

    writeInitial(settings, ConfigVersion);
}
