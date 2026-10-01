#ifndef PROFILESETTINGS_H
#define PROFILESETTINGS_H

#include <QSettings>
#include <QString>

#include "application/applicationconstants.h"

class SecretStore;

// The single definition of every setting stored in a profile's INI file:
// its key, the value a new profile starts with, and the value assumed when
// the key is missing. The last two differ for a few settings, so that
// profiles written by older versions keep behaving the way they did.
namespace ProfileSettings
{
template <typename T>
struct Key
{
    using Value = T;

    Key(const char *name, T value)
        : name(name),
          initial(value),
          fallback(value)
    {
    }

    Key(const char *name, T initial, T fallback)
        : name(name),
          initial(initial),
          fallback(fallback)
    {
    }

    const char *name;
    T initial;
    T fallback;
};

// A secret. A separate type keeps it from being read or written as an
// ordinary setting: it lives in the system credential store when there is
// one, and in the profile file otherwise.
struct SecretKey
{
    const char *name;
    // How the value is kept when it has to stay in the profile file.
    bool base64InFile;
};

// Credentials
inline const Key<QString> Username{"Credential/Username", ""};
inline const SecretKey Password{"Credential/Password", true};
inline const SecretKey TOTPSecret{"Credential/TOTPSecret", false};
inline const Key<QString> CertFile{"Credential/CertFile", ""};
inline const SecretKey CertPassword{"Credential/CertPassword", true};
// Names this profile's entries in the system credential store. It stays the
// same when the profile is renamed.
inline const Key<QString> SecretId{"Credential/SecretId", ""};

// Application behaviour
inline const Key<int> ConfigVersion{
    "Common/ConfigVersion", ApplicationConstants::ConfigVersion, -1
};
inline const Key<bool> ConnectAfterStart{"Common/ConnectAfterStart", false};
inline const Key<bool> CheckUpdateAfterStart{"Common/CheckUpdateAfterStart", false};
inline const Key<bool> AutoSetProxy{"Common/AutoSetProxy", false};
inline const Key<bool> AutoReconnect{"Common/AutoReconnect", false};
inline const Key<int> ReconnectTime{"Common/ReconnectTime", 1};
inline const Key<QString> SystemProxyBypass{"Common/SystemProxyBypass", ""};
inline const Key<bool> SuppressProxyOverrideWarning{
    "Common/SuppressProxyOverrideWarning", false
};
// Config version 6 kept this per profile; it now lives in the global state.
inline const Key<bool> LegacyAutoStart{"Common/AutoStart", false};

// Server and authentication
inline const Key<QString> ServerAddress{"ZJUConnect/ServerAddress", "trust.hitsz.edu.cn", ""};
inline const Key<int> ServerPort{"ZJUConnect/ServerPort", 443};
// Profiles from before aTrust support have no protocol and are EasyConnect.
inline const Key<QString> Protocol{"ZJUConnect/Protocol", "atrust", "easyconnect"};
// Read through easyConnectAuthType(), which infers a missing value.
inline const Key<QString> EasyConnectAuthType{"ZJUConnect/EasyConnectAuthType", "password"};
inline const Key<QString> AuthType{"ZJUConnect/AuthType", "cas", "psw"};
inline const Key<QString> LoginDomain{"ZJUConnect/LoginDomain", "hitcas", ""};
inline const Key<QString> LoginURL{"ZJUConnect/LoginURL", ""};
inline const Key<QString> PhoneCountryCode{"ZJUConnect/PhoneCountryCode", "86"};
inline const Key<QString> PhoneNumber{"ZJUConnect/PhoneNumber", ""};
inline const Key<bool> CredentialsAsArguments{"ZJUConnect/CredentialsAsArguments", false};

// DNS
inline const Key<QString> DNS{"ZJUConnect/DNS", ""};
inline const Key<bool> DNSAuto{"ZJUConnect/DNSAuto", true, false};
inline const Key<QString> SecondaryDNS{"ZJUConnect/SecondaryDNS", ""};
inline const Key<QString> LocalDNSServer{"ZJUConnect/LocalDNSServer", ""};
inline const Key<QString> DNSServerBind{"ZJUConnect/DNSServerBind", ""};
inline const Key<int> DNSTTL{"ZJUConnect/DNSTTL", 3600};
inline const Key<bool> DisableZJUDNS{"ZJUConnect/DisableZJUDNS", false};
inline const Key<QString> CustomDNS{"ZJUConnect/CustomDNS", ""};

// Local proxies
inline const Key<int> SOCKS5Port{"ZJUConnect/SOCKS5Port", 11080};
inline const Key<int> HTTPPort{"ZJUConnect/HTTPPort", 11081};
inline const Key<bool> OutsideAccess{"ZJUConnect/OutsideAccess", false};
inline const Key<QString> ShadowsocksURL{"ZJUConnect/ShadowsocksURL", ""};
inline const Key<QString> DialDirectProxy{"ZJUConnect/DialDirectProxy", ""};
inline const Key<bool> ProxyAll{"ZJUConnect/ProxyAll", false};
inline const Key<QString> CustomProxyDomain{"ZJUConnect/CustomProxyDomain", ""};

// Tunnel
inline const Key<bool> TUNMode{"ZJUConnect/TUNMode", false};
inline const Key<bool> AddRoute{"ZJUConnect/AddRoute", false};
inline const Key<bool> DNSHijack{"ZJUConnect/DNSHijack", false};
inline const Key<bool> FakeIP{"ZJUConnect/FakeIP", false};
inline const Key<bool> TCPTunnelMode{"ZJUConnect/TCPTunnelMode", false};
inline const Key<QString> TCPPortForwarding{"ZJUConnect/TCPPortForwarding", ""};
inline const Key<QString> UDPPortForwarding{"ZJUConnect/UDPPortForwarding", ""};

// Connection behaviour
inline const Key<int> UpdateBestNodesInterval{"ZJUConnect/UpdateBestNodesInterval", 300};
inline const Key<bool> MultiLine{"ZJUConnect/MultiLine", false};
inline const Key<bool> KeepAlive{"ZJUConnect/KeepAlive", false};
inline const Key<QString> KeepAliveURL{"ZJUConnect/KeepAliveURL", ""};
inline const Key<QString> BindInterface{"ZJUConnect/BindInterface", ""};
inline const Key<bool> AutoDetectInterface{"ZJUConnect/AutoDetectInterface", false};
inline const Key<bool> SkipDomainResource{"ZJUConnect/SkipDomainResource", false};
inline const Key<bool> DisableServerConfig{"ZJUConnect/DisableServerConfig", false};
inline const Key<bool> ZJUDefault{"ZJUConnect/ZJUDefault", false};
inline const Key<QString> ExtraArguments{"ZJUConnect/ExtraArguments", ""};

// Debugging
inline const Key<bool> Debug{"ZJUConnect/Debug", false};
inline const Key<bool> DebugPCAP{"ZJUConnect/DebugPCAP", false};
inline const Key<bool> DebugTLSLog{"ZJUConnect/DebugTLSLog", false};

template <typename T>
T read(const QSettings &settings, const Key<T> &key)
{
    return settings.value(key.name, key.fallback).template value<T>();
}

// The value parameter does not take part in deduction, so a string literal
// can be written to a QString setting.
template <typename T>
void write(QSettings &settings, const Key<T> &key, const typename Key<T>::Value &value)
{
    settings.setValue(key.name, value);
}

template <typename T>
void writeInitial(QSettings &settings, const Key<T> &key)
{
    settings.setValue(key.name, key.initial);
}

template <typename T>
bool contains(const QSettings &settings, const Key<T> &key)
{
    return settings.contains(key.name);
}

// Without a store, or with nullptr, secrets stay in the profile file. The
// store is not owned and must outlive every later call.
void setSecretStore(SecretStore *store);

bool usesSecretStore();

QString read(const QSettings &settings, const SecretKey &key);
void write(QSettings &settings, const SecretKey &key, const QString &secret);
// For forms: writes only a secret the user changed. A secret that could not
// be read shows up empty, and writing that back would erase the stored one.
void writeIfChanged(
    QSettings &settings,
    const SecretKey &key,
    const QString &loaded,
    const QString &current
);

// Moves secrets that are still in the profile file into the store.
void migrateSecrets(QSettings &settings);
// Removes a profile's secrets from the store, before the profile is deleted
// or its settings are cleared. False means some of them are still there.
bool forgetSecrets(QSettings &settings);
bool forgetSecrets(const QString &secretId);
// Removes everything that must not leave this machine from a copy of a
// profile file: the secrets and the identifier of their store entries.
void stripSecrets(QSettings &exportedCopy);
// Gives a profile file that was copied from another profile its own copies
// of that profile's secrets, so that editing one does not change the other.
void detachSecrets(QSettings &settings);

// Profiles saved before the authentication type was stored used certificate
// authentication exactly when a certificate file was configured.
inline QString easyConnectAuthType(const QSettings &settings)
{
    if (contains(settings, EasyConnectAuthType))
    {
        return settings.value(EasyConnectAuthType.name).toString();
    }
    return read(settings, CertFile).isEmpty() ? "password" : "certificate";
}
}

#endif // PROFILESETTINGS_H
