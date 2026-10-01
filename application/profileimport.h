#ifndef PROFILEIMPORT_H
#define PROFILEIMPORT_H

#include <QString>

class QSettings;

// Replaces a profile with the contents of an exported file.
namespace ProfileImport
{
// The extra core arguments the file asks for. They are passed to the core
// verbatim, in TUN mode to a core running as root, so a file from someone
// else should not get to set them without the user seeing them.
QString extraArguments(const QSettings &source);

// The profile becomes the defaults plus whatever the file sets, so nothing
// the previous profile had (TUN mode, debug options, saved secrets) survives
// unless the file asks for it.
void apply(QSettings &destination, const QSettings &source, bool includeExtraArguments);
}

#endif // PROFILEIMPORT_H
