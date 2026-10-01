#include <QCoreApplication>
#include <QDebug>

#include "infrastructure/platform/autostart.h"

namespace
{
bool expectEqual(const QString &actual, const QString &expected, const char *testName)
{
    if (actual == expected)
    {
        return true;
    }
    qCritical().noquote() << testName << "failed"
                          << "\nexpected:" << expected
                          << "\nactual:  " << actual;
    return false;
}

bool quotesTheWindowsRunCommand()
{
    return expectEqual(
        AutoStart::windowsRunCommand(R"(C:\Program Files\EZ4 Connect\EZ4Connect.exe)"),
        R"("C:\Program Files\EZ4 Connect\EZ4Connect.exe")",
        "quotesTheWindowsRunCommand"
    );
}

bool escapesTheDesktopEntryExecLine()
{
    return expectEqual(
            AutoStart::desktopEntryExec("/opt/My Apps/EZ4Connect"),
            R"("/opt/My Apps/EZ4Connect")",
            "quotesSpacesInTheDesktopEntry"
        )
        && expectEqual(
            AutoStart::desktopEntryExec(R"(/opt/a"b`c$d\e%f/EZ4Connect)"),
            R"("/opt/a\\"b\\`c\\$d\\\\e%%f/EZ4Connect")",
            "escapesSpecialCharactersInTheDesktopEntry"
        );
}

bool escapesAppleScriptStrings()
{
    return expectEqual(
        AutoStart::appleScriptString(R"(/Applications/My "VPN"\EZ4Connect.app)"),
        R"("/Applications/My \"VPN\"\\EZ4Connect.app")",
        "escapesAppleScriptStrings"
    );
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    return quotesTheWindowsRunCommand()
        && escapesTheDesktopEntryExecLine()
        && escapesAppleScriptStrings() ? 0 : 1;
}
