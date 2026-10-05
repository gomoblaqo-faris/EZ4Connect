#include <QCoreApplication>
#include <QDebug>

#include "application/wording.h"

namespace
{
bool expect(const Wording &wording, const QString &english, const char *what)
{
    // No translator is installed, so both wordings are the English text.
    if (wording.english != english || wording.translated != english)
    {
        qCritical() << what << "gave" << wording.english << "/" << wording.translated
                    << "instead of" << english;
        return false;
    }
    return true;
}

bool fillsInTheArgumentsGiven()
{
    return expect(Wording::of("Test", "No arguments"), "No arguments", "no arguments")
        && expect(Wording::of("Test", "Error: %1", "busy"), "Error: busy", "one argument")
        && expect(Wording::of("Test", "%1: %2", "a", "b"), "a: b", "two arguments");
}

bool treatsAnEmptyArgumentAsAnArgument()
{
    // A command that failed without saying why still fills the placeholder.
    return expect(Wording::of("Test", "Error: %1", QString()), "Error: ", "a null argument")
        && expect(Wording::of("Test", "Error: %1", QStringLiteral("")), "Error: ", "an empty argument");
}

bool leavesPlaceholdersInsideArgumentsAlone()
{
    return expect(Wording::of("Test", "%1: %2", "100%2 done", "b"), "100%2 done: b",
                  "a first argument containing %2");
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    return fillsInTheArgumentsGiven()
               && treatsAnEmptyArgumentAsAnArgument()
               && leavesPlaceholdersInsideArgumentsAlone()
           ? 0
           : 1;
}
