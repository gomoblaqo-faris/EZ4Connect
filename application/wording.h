#ifndef WORDING_H
#define WORDING_H

#include <QCoreApplication>
#include <QString>

// A message in two wordings: English, for the log and the command-line
// client, and the interface language, for showing to the user. The source
// text is marked with QT_TRANSLATE_NOOP where it is written, so that lupdate
// finds it, and the arguments go into both wordings.
struct Wording
{
    QString english;
    QString translated;

    static Wording of(const char *context, const char *source)
    {
        return {QString::fromUtf8(source), QCoreApplication::translate(context, source)};
    }

    static Wording of(const char *context, const char *source, const QString &first)
    {
        Wording wording = of(context, source);
        return {wording.english.arg(first), wording.translated.arg(first)};
    }

    // One call with both arguments, so that a "%2" inside the first one is
    // not taken for a placeholder.
    static Wording of(
        const char *context,
        const char *source,
        const QString &first,
        const QString &second
    )
    {
        Wording wording = of(context, source);
        return {wording.english.arg(first, second), wording.translated.arg(first, second)};
    }
};

#endif // WORDING_H
