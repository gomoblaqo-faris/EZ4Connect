#ifndef OPERATIONSTATUS_H
#define OPERATIONSTATUS_H

#include <QString>

// Outcome of a platform operation. The error is worded for the user, because
// only the presentation layer is allowed to show it.
struct OperationStatus
{
    bool succeeded = true;
    QString error;

    static OperationStatus failure(const QString &error)
    {
        return {false, error};
    }
};

#endif // OPERATIONSTATUS_H
