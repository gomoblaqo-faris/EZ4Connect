#ifndef SECRETSTORE_H
#define SECRETSTORE_H

#include <QString>

#include <optional>

// Port for the operating system's credential store.
class SecretStore
{
public:
    virtual ~SecretStore() = default;

    // No value means there is no such entry or it could not be read.
    virtual std::optional<QString> read(const QString &account) = 0;
    virtual bool write(const QString &account, const QString &secret) = 0;
    // Removing an entry that does not exist counts as success.
    virtual bool remove(const QString &account) = 0;
};

#endif // SECRETSTORE_H
