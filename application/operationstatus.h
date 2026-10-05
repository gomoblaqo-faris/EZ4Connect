#ifndef OPERATIONSTATUS_H
#define OPERATIONSTATUS_H

#include <QString>

#include "application/wording.h"

// Outcome of a platform operation. Only the presentation layer shows the
// error, in the interface language; the log keeps the English wording.
struct OperationStatus
{
    bool succeeded = true;
    // English, for the log and the command-line client.
    QString error;
    // The same error in the interface language, when it was worded for that.
    QString translatedError;

    QString errorForDisplay() const
    {
        return translatedError.isEmpty() ? error : translatedError;
    }

    static OperationStatus failure(const QString &error)
    {
        return {false, error, QString()};
    }

    static OperationStatus failure(const Wording &wording)
    {
        return {false, wording.english, wording.translated};
    }
};

#endif // OPERATIONSTATUS_H
