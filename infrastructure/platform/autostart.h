#ifndef AUTOSTART_H
#define AUTOSTART_H

#include "application/operationstatus.h"

namespace AutoStart
{
OperationStatus setEnabled(bool enabled);

// How a path or name is written into each platform's start-up entry. Install
// locations contain spaces often enough, and quotes, backslashes, "$" and
// "%" occasionally.
QString windowsRunCommand(const QString &executablePath);
QString desktopEntryExec(const QString &executablePath);
QString appleScriptString(const QString &text);
}

#endif // AUTOSTART_H
