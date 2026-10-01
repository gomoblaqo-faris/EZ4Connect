#include <QDebug>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include "systemproxyfunctions.h"

#if defined(Q_OS_WINDOWS)
#include "windows.h"
#include "wininet.h"
#include "ras.h"
#include "raserror.h"
#endif

const QString macOSNetworkSetupPath = "/usr/sbin/networksetup";

#if defined(Q_OS_WINDOWS)
// Applications that are already running keep using the old settings until
// they are told that the settings changed.
void windowsNotifyProxySettingsChanged()
{
    InternetSetOption(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
    InternetSetOption(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
}
#endif

OperationStatus windowsSetProxyForAllConnections(const QString &proxyServer, const QString &bypass)
{
#if defined(Q_OS_WINDOWS)
    INTERNET_PER_CONN_OPTION_LIST optionList;
    INTERNET_PER_CONN_OPTION optionsArr[3];
    unsigned long optionListSize = sizeof(INTERNET_PER_CONN_OPTION_LIST);

    optionsArr[1].dwOption = INTERNET_PER_CONN_FLAGS;
    optionsArr[1].Value.dwValue = PROXY_TYPE_DIRECT | PROXY_TYPE_PROXY;

    optionsArr[0].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
    auto *proxyServerWStr = (wchar_t *)calloc(sizeof(wchar_t), proxyServer.length() + 1);
    proxyServer.toWCharArray(proxyServerWStr);
    optionsArr[0].Value.pszValue = proxyServerWStr;

    optionsArr[2].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
    auto *bypassWStr = (wchar_t *)calloc(sizeof(wchar_t), bypass.length() + 1);
    bypass.toWCharArray(bypassWStr);
    optionsArr[2].Value.pszValue = bypassWStr;

    optionList.dwSize = sizeof(INTERNET_PER_CONN_OPTION_LIST);
    optionList.pszConnection = nullptr;
    optionList.dwOptionCount = 3;
    optionList.dwOptionError = 0;
    optionList.pOptions = optionsArr;

    // Dial-up entries below are best effort; the default connection decides
    // whether the change took effect.
    OperationStatus status;
    if (!InternetSetOption(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &optionList, optionListSize))
    {
        status = OperationStatus::failure(
            QString("InternetSetOption failed with error %1").arg(GetLastError())
        );
    }

    DWORD dwCb = 0;
    DWORD dwRet = ERROR_SUCCESS;
    DWORD dwEntries = 0;
    LPRASENTRYNAME lpRasEntryName = nullptr;

    dwRet = RasEnumEntries(nullptr, nullptr, lpRasEntryName, &dwCb, &dwEntries);

    if (dwRet == ERROR_BUFFER_TOO_SMALL)
    {
        lpRasEntryName = (LPRASENTRYNAME)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwCb);
        if (lpRasEntryName == nullptr)
        {
            free(proxyServerWStr);
            free(bypassWStr);
            return status;
        }
        lpRasEntryName[0].dwSize = sizeof(RASENTRYNAME);

        dwRet = RasEnumEntries(nullptr, nullptr, lpRasEntryName, &dwCb, &dwEntries);

        if (ERROR_SUCCESS == dwRet)
        {
            for (DWORD i = 0; i < dwEntries; i++)
            {
                optionList.pszConnection = lpRasEntryName[i].szEntryName;
                InternetSetOption(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &optionList, optionListSize);
            }
        }

        HeapFree(GetProcessHeap(), 0, lpRasEntryName);
    }

    windowsNotifyProxySettingsChanged();
    free(proxyServerWStr);
    free(bypassWStr);
    return status;
#else
    Q_UNUSED(proxyServer)
    Q_UNUSED(bypass)
    return {};
#endif
}

OperationStatus windowsClearProxyForAllConnections()
{
#if defined(Q_OS_WINDOWS)
    INTERNET_PER_CONN_OPTION_LIST optionList;
    INTERNET_PER_CONN_OPTION optionsArr[1];
    unsigned long optionListSize = sizeof(INTERNET_PER_CONN_OPTION_LIST);

    optionsArr[0].dwOption = INTERNET_PER_CONN_FLAGS;
    optionsArr[0].Value.dwValue = PROXY_TYPE_DIRECT;

    optionList.dwSize = sizeof(INTERNET_PER_CONN_OPTION_LIST);
    optionList.pszConnection = nullptr;
    optionList.dwOptionCount = 1;
    optionList.dwOptionError = 0;
    optionList.pOptions = optionsArr;

    // Dial-up entries below are best effort; the default connection decides
    // whether the change took effect.
    OperationStatus status;
    if (!InternetSetOption(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &optionList, optionListSize))
    {
        status = OperationStatus::failure(
            QString("InternetSetOption failed with error %1").arg(GetLastError())
        );
    }

    DWORD dwCb = 0;
    DWORD dwRet = ERROR_SUCCESS;
    DWORD dwEntries = 0;
    LPRASENTRYNAME lpRasEntryName = nullptr;

    dwRet = RasEnumEntries(nullptr, nullptr, lpRasEntryName, &dwCb, &dwEntries);

    if (dwRet == ERROR_BUFFER_TOO_SMALL)
    {
        lpRasEntryName = (LPRASENTRYNAME)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwCb);
        if (lpRasEntryName == nullptr)
            return status;
        lpRasEntryName[0].dwSize = sizeof(RASENTRYNAME);

        dwRet = RasEnumEntries(nullptr, nullptr, lpRasEntryName, &dwCb, &dwEntries);

        if (ERROR_SUCCESS == dwRet)
        {
            for (DWORD i = 0; i < dwEntries; i++)
            {
                optionList.pszConnection = lpRasEntryName[i].szEntryName;
                InternetSetOption(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &optionList, optionListSize);
            }
        }

        HeapFree(GetProcessHeap(), 0, lpRasEntryName);
    }
    windowsNotifyProxySettingsChanged();
    return status;
#else
    return {};
#endif
}

OperationStatus runNetworkSetup(
    const QStringList &arguments,
    const QString &failure,
    QString *output = nullptr
)
{
    QProcess process;
    process.start(macOSNetworkSetupPath, arguments);
    process.waitForFinished();
    if (process.error() != QProcess::UnknownError)
    {
        return OperationStatus::failure(failure + ": " + process.errorString());
    }
    if (process.exitCode() != 0)
    {
        // networksetup reports some errors on standard output.
        const QString details = QString::fromLocal8Bit(
            process.readAllStandardError() + process.readAllStandardOutput()
        ).trimmed();
        return OperationStatus::failure(failure + ": " + details);
    }
    if (output != nullptr)
    {
        *output = QString::fromLocal8Bit(process.readAllStandardOutput());
    }
    return {};
}

OperationStatus macOSGetActiveNetworkServices(QStringList *activeServices)
{
    QString output;
    const OperationStatus status = runNetworkSetup(
        {"-listallnetworkservices"},
        "Could not list network services",
        &output
    );
    if (!status.succeeded)
    {
        return status;
    }
    /*
    output will be like this:

    An asterisk (*) denotes that a network service is disabled.
    USB 10/100/1000 LAN
    *AX88179A
    Thunderbolt Bridge
    Wi-Fi
    iPhone USB
    */
    qDebug() << output;
    QStringList lines = output.split('\n');
    lines.removeFirst();
    for (const QString &line : lines)
    {
        if (line.isEmpty())
            continue;
        if (line.startsWith("*"))
            continue;
        activeServices->push_back(line);
    }
    return {};
}

enum class macOSProxyType
{
    WebProxy,
    SecureWebProxy,
    SOCKSFirewallProxy
};

bool macOSIsSystemProxySet(macOSProxyType proxyType, const QString networkService, int port)
{
    QStringList args;
    switch (proxyType)
    {
    case macOSProxyType::WebProxy:
        args << "-getwebproxy";
        break;
    case macOSProxyType::SecureWebProxy:
        args << "-getsecurewebproxy";
        break;
    case macOSProxyType::SOCKSFirewallProxy:
        args << "-getsocksfirewallproxy";
        break;
    }
    args << networkService;
    QString output;
    const OperationStatus status = runNetworkSetup(
        args,
        "Could not read system proxy settings",
        &output
    );
    if (!status.succeeded)
    {
        // Settings that cannot be read may belong to another app, so ask
        // before overwriting them.
        qWarning().noquote() << status.error;
        return true;
    }
    if (output.contains("Enabled: Yes")) {
        if (output.contains("Server: 127.0.0.1") && output.contains("Port: " + QString::number(port))) {
            return false;
        }
        return true;
    }
    return false;
}

OperationStatus macOSSetSystemProxy(macOSProxyType proxyType, const QString &networkService, const QString &proxyServer, int port)
{
    QStringList setArgs, enableArgs;
    switch (proxyType)
    {
    case macOSProxyType::WebProxy:
        setArgs << "-setwebproxy";
        enableArgs << "-setwebproxystate";
        break;
    case macOSProxyType::SecureWebProxy:
        setArgs << "-setsecurewebproxy";
        enableArgs << "-setsecurewebproxystate";
        break;
    case macOSProxyType::SOCKSFirewallProxy:
        setArgs << "-setsocksfirewallproxy";
        enableArgs << "-setsocksfirewallproxystate";
        break;
    }
    setArgs << networkService << proxyServer << QString::number(port);
    const OperationStatus status = runNetworkSetup(
        setArgs,
        "Could not set the system proxy for " + networkService
    );
    if (!status.succeeded)
    {
        return status;
    }
    enableArgs << networkService << "on";
    return runNetworkSetup(
        enableArgs,
        "Could not enable the system proxy for " + networkService
    );
}

OperationStatus macOSDisableSystemProxy(macOSProxyType proxyType, const QString &networkService)
{
    QStringList args;
    switch (proxyType)
    {
    case macOSProxyType::WebProxy:
        args << "-setwebproxystate";
        break;
    case macOSProxyType::SecureWebProxy:
        args << "-setsecurewebproxystate";
        break;
    case macOSProxyType::SOCKSFirewallProxy:
        args << "-setsocksfirewallproxystate";
        break;
    }
    args << networkService << "off";
    return runNetworkSetup(
        args,
        "Could not disable the system proxy for " + networkService
    );
}

QStringList macOSProxyBypassDomains(const QString &bypass)
{
    // networksetup takes one argument per domain, and "Empty" clears the list.
    QStringList domains;
    for (const QString &domain : bypass.split(';', Qt::SkipEmptyParts))
    {
        const QString trimmedDomain = domain.trimmed();
        if (!trimmedDomain.isEmpty())
        {
            domains << trimmedDomain;
        }
    }
    if (domains.isEmpty())
    {
        domains << "Empty";
    }
    return domains;
}

OperationStatus macOSSetProxyBypass(const QString &networkService, const QString &bypass)
{
    QStringList args;
    args << "-setproxybypassdomains";
    args << networkService;
    args << macOSProxyBypassDomains(bypass);
    return runNetworkSetup(
        args,
        "Could not set proxy bypass domains for " + networkService
    );
}

struct LinuxProxyCommand
{
    QString program;
    QStringList arguments;
    // Whether the proxy cannot be considered changed if this command fails.
    bool required;
};

OperationStatus runLinuxProxyCommands(const QList<LinuxProxyCommand> &commands, const QString &failure)
{
    // Run every command even after a failure, so the desktop is left as close
    // to the requested state as possible.
    QStringList failedPrograms;
    for (const LinuxProxyCommand &command : commands)
    {
        const int exitCode = QProcess::execute(command.program, command.arguments);
        qDebug() << QStringLiteral("[%1] Program: %2, Args: %3").arg(exitCode).arg(command.program).arg(command.arguments.join(";"));
        if (exitCode != 0 && command.required && !failedPrograms.contains(command.program))
        {
            failedPrograms << command.program;
        }
    }
    if (failedPrograms.isEmpty())
    {
        return {};
    }
    return OperationStatus::failure(failure + ": " + failedPrograms.join(", ") + " failed");
}

bool linuxSessionIsKDE()
{
    return qEnvironmentVariable("XDG_SESSION_DESKTOP") == "KDE" ||
           qEnvironmentVariable("XDG_SESSION_DESKTOP") == "plasma";
}

OperationStatus linuxSetSystemProxy(const QString &proxyServer, int httpPort, int socksPort, const QString &bypass)
{
    // A KDE session reads kioslaverc, and gsettings may not even be
    // installed there, so only the session's own tool has to succeed.
    const bool isKDE = linuxSessionIsKDE();
    const bool gnomeRequired = !isKDE;
    const QString kioslaverc = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/kioslaverc";
    const QString kwriteconfigName = "kwriteconfig" + qEnvironmentVariable("KDE_SESSION_VERSION");

    QList<LinuxProxyCommand> commands;
    commands << LinuxProxyCommand{"gsettings", {"set", "org.gnome.system.proxy", "mode", "manual"}, gnomeRequired};

    // Configure HTTP Proxies for HTTP, FTP and HTTPS
    for (const auto &protocol : QStringList{"http", "ftp", "https"})
    {
        commands << LinuxProxyCommand{"gsettings",
                                      {"set", "org.gnome.system.proxy." + protocol, "host", proxyServer},
                                      gnomeRequired};
        commands << LinuxProxyCommand{"gsettings",
                                      {"set", "org.gnome.system.proxy." + protocol, "port", QString::number(httpPort)},
                                      gnomeRequired};
        if (isKDE)
        {
            commands << LinuxProxyCommand{kwriteconfigName,
                                          {"--file", kioslaverc,
                                           "--group", "Proxy Settings",
                                           "--key", protocol + "Proxy",
                                           "http://" + proxyServer + " " + QString::number(httpPort)},
                                          true};
        }
    }

    // Configure SOCKS5 Proxies
    commands << LinuxProxyCommand{"gsettings", {"set", "org.gnome.system.proxy.socks", "host", proxyServer}, gnomeRequired};
    commands << LinuxProxyCommand{"gsettings",
                                  {"set", "org.gnome.system.proxy.socks", "port", QString::number(socksPort)},
                                  gnomeRequired};
    if (isKDE)
    {
        commands << LinuxProxyCommand{kwriteconfigName,
                                      {"--file", kioslaverc,
                                       "--group", "Proxy Settings",
                                       "--key", "socksProxy",
                                       "socks://" + proxyServer + " " + QString::number(socksPort)},
                                      true};
    }

    // Setting Proxy Mode to Manual
    commands << LinuxProxyCommand{"gsettings", {"set", "org.gnome.system.proxy", "mode", "manual"}, gnomeRequired};
    const QStringList bypassList = bypass.split(";");
    const QString ignoreHosts = "[\"" + bypassList.join("\",\"") + "\"]";
    commands << LinuxProxyCommand{"gsettings", {"set", "org.gnome.system.proxy", "ignore-hosts", ignoreHosts}, gnomeRequired};
    if (isKDE)
    {
        commands << LinuxProxyCommand{kwriteconfigName,
                                      {"--file", kioslaverc,
                                       "--group", "Proxy Settings",
                                       "--key", "ProxyType", "1"},
                                      true};
        commands << LinuxProxyCommand{kwriteconfigName,
                                      {"--file", kioslaverc,
                                       "--group", "Proxy Settings",
                                       "--key", "NoProxyFor", bypass},
                                      true};
        // Notify kioslaves to reload system proxy configuration.
        commands << LinuxProxyCommand{"dbus-send",
                                      {"--type=signal", "/KIO/Scheduler",
                                       "org.kde.KIO.Scheduler.reparseSlaveConfiguration",
                                       "string:''"},
                                      false};
    }

    return runLinuxProxyCommands(commands, "Could not set the system proxy");
}

OperationStatus linuxClearSystemProxy()
{
    const bool isKDE = linuxSessionIsKDE();
    const QString kioslaverc = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/kioslaverc";
    const QString kwriteconfigName = "kwriteconfig" + qEnvironmentVariable("KDE_SESSION_VERSION");

    // Setting System Proxy Mode to: None
    QList<LinuxProxyCommand> commands;
    commands << LinuxProxyCommand{"gsettings", {"set", "org.gnome.system.proxy", "mode", "none"}, !isKDE};
    if (isKDE)
    {
        commands << LinuxProxyCommand{kwriteconfigName,
                                      {"--file", kioslaverc,
                                       "--group", "Proxy Settings",
                                       "--key", "ProxyType", "0"},
                                      true};
        // Notify kioslaves to reload system proxy configuration.
        commands << LinuxProxyCommand{"dbus-send",
                                      {"--type=signal", "/KIO/Scheduler",
                                       "org.kde.KIO.Scheduler.reparseSlaveConfiguration",
                                       "string:''"},
                                      false};
    }

    return runLinuxProxyCommands(commands, "Could not clear the system proxy");
}

bool linuxIsSystemProxySet(int http_port, int socks_port)
{
    if (linuxSessionIsKDE())
    {
        QString KDEver = qEnvironmentVariable("KDE_SESSION_VERSION");
        QString kreadconfigName = "kreadconfig" + KDEver;
        const auto configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);

        QProcess process;
        process.start(kreadconfigName, {"--file", configPath + "/kioslaverc",
                                        "--group", "Proxy Settings",
                                        "--key", "ProxyType"});
        process.waitForFinished();
        if (process.readAllStandardOutput().trimmed() != "1")
            return false;

        if (http_port > 0)
        {
            QProcess httpProcess;
            httpProcess.start(kreadconfigName, {"--file", configPath + "/kioslaverc",
                                                "--group", "Proxy Settings",
                                                "--key", "httpProxy"});
            httpProcess.waitForFinished();
            if (httpProcess.readAllStandardOutput().trimmed() == "http://127.0.0.1 " + QString::number(http_port))
                return false;
        }
        return true;
    }

    QProcess modeProcess;
    modeProcess.start("gsettings", {"get", "org.gnome.system.proxy", "mode"});
    modeProcess.waitForFinished();
    if (!modeProcess.readAllStandardOutput().contains("manual"))
        return false;

    if (http_port > 0)
    {
        QProcess hostProcess;
        hostProcess.start("gsettings", {"get", "org.gnome.system.proxy.http", "host"});
        hostProcess.waitForFinished();
        QProcess portProcess;
        portProcess.start("gsettings", {"get", "org.gnome.system.proxy.http", "port"});
        portProcess.waitForFinished();
        if (hostProcess.readAllStandardOutput().trimmed().contains("127.0.0.1") &&
            portProcess.readAllStandardOutput().trimmed() == QString::number(http_port))
        {
            // The HTTP proxy is this app's own, but the SOCKS proxy may
            // still be somebody else's and would be overwritten too.
            QProcess socksHostProcess;
            socksHostProcess.start("gsettings", {"get", "org.gnome.system.proxy.socks", "host"});
            socksHostProcess.waitForFinished();
            QProcess socksPortProcess;
            socksPortProcess.start("gsettings", {"get", "org.gnome.system.proxy.socks", "port"});
            socksPortProcess.waitForFinished();
            const QByteArray socksHost = socksHostProcess.readAllStandardOutput().trimmed();
            const bool socksUnset = socksHost.isEmpty() || socksHost == "''";
            const bool socksIsOurs = socksHost.contains("127.0.0.1")
                && socksPortProcess.readAllStandardOutput().trimmed() == QString::number(socks_port);
            return !(socksUnset || socksIsOurs);
        }
    }
    return true;
}

bool PlatformSystemProxy::isSet(int http_port, int socks_port)
{
#if defined(Q_OS_WINDOWS)
    Q_UNUSED(socks_port)
    QSettings proxySettings(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Internet Settings)",
                            QSettings::NativeFormat);
    if (proxySettings.value("ProxyEnable", 0).toInt() != 1)
        return false;
    // if same settings
    if (http_port > 0 && proxySettings.value("ProxyServer").toString() == "127.0.0.1:" + QString::number(http_port))
        return false;
    return true;
#elif defined(Q_OS_MACOS)
    QStringList activeServices;
    const OperationStatus status = macOSGetActiveNetworkServices(&activeServices);
    if (!status.succeeded)
    {
        qWarning().noquote() << status.error;
        return false;
    }
    for (const QString &service : activeServices)
    {
        if (macOSIsSystemProxySet(macOSProxyType::WebProxy, service, http_port))
            return true;
        if (macOSIsSystemProxySet(macOSProxyType::SecureWebProxy, service, http_port))
            return true;
        if (macOSIsSystemProxySet(macOSProxyType::SOCKSFirewallProxy, service, socks_port))
            return true;
    }
    return false;
#elif defined(Q_OS_LINUX)
    return linuxIsSystemProxySet(http_port, socks_port);
#else
    Q_UNUSED(http_port)
    Q_UNUSED(socks_port)
    return false;
#endif
}

OperationStatus PlatformSystemProxy::set(int http_port, int socks_port, const QString &bypass)
{
#if defined(Q_OS_WINDOWS)
    Q_UNUSED(socks_port)
    return windowsSetProxyForAllConnections(
        "127.0.0.1:" + QString::number(http_port),
        bypass);
#elif defined(Q_OS_MACOS)
    QStringList activeServices;
    OperationStatus status = macOSGetActiveNetworkServices(&activeServices);
    if (!status.succeeded)
    {
        return status;
    }
    if (activeServices.isEmpty())
    {
        return OperationStatus::failure("Could not set the system proxy: no active network service was found");
    }
    for (const QString &service : activeServices)
    {
        status = macOSSetSystemProxy(macOSProxyType::WebProxy, service, "127.0.0.1", http_port);
        if (status.succeeded)
        {
            status = macOSSetSystemProxy(macOSProxyType::SecureWebProxy, service, "127.0.0.1", http_port);
        }
        if (status.succeeded)
        {
            status = macOSSetSystemProxy(macOSProxyType::SOCKSFirewallProxy, service, "127.0.0.1", socks_port);
        }
        if (status.succeeded)
        {
            status = macOSSetProxyBypass(service, bypass);
        }
        if (!status.succeeded)
        {
            return status;
        }
    }
    return {};
#elif defined(Q_OS_LINUX)
    return linuxSetSystemProxy("127.0.0.1", http_port, socks_port, bypass);
#else
    Q_UNUSED(http_port)
    Q_UNUSED(socks_port)
    Q_UNUSED(bypass)
    return OperationStatus::failure("Setting the system proxy is not supported on this platform");
#endif
}

OperationStatus PlatformSystemProxy::clear()
{
#if defined(Q_OS_WINDOWS)
    return windowsClearProxyForAllConnections();
#elif defined(Q_OS_MACOS)
    QStringList activeServices;
    const OperationStatus listStatus = macOSGetActiveNetworkServices(&activeServices);
    if (!listStatus.succeeded)
    {
        return listStatus;
    }
    // Keep going after a failure so as few services as possible are left
    // pointing at a proxy that is about to stop.
    OperationStatus firstFailure;
    for (const QString &service : activeServices)
    {
        for (const macOSProxyType proxyType : {macOSProxyType::WebProxy,
                                               macOSProxyType::SecureWebProxy,
                                               macOSProxyType::SOCKSFirewallProxy})
        {
            const OperationStatus status = macOSDisableSystemProxy(proxyType, service);
            if (!status.succeeded && firstFailure.succeeded)
            {
                firstFailure = status;
            }
        }
    }
    return firstFailure;
#elif defined(Q_OS_LINUX)
    return linuxClearSystemProxy();
#else
    return OperationStatus::failure("Clearing the system proxy is not supported on this platform");
#endif
}
