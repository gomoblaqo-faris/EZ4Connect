#ifndef KEYCHAINSECRETSTORE_H
#define KEYCHAINSECRETSTORE_H

#include "application/secretstore.h"

// Keeps secrets in the macOS Keychain, Windows Credential Manager or the
// Linux Secret Service, through QtKeychain.
class KeychainSecretStore : public SecretStore
{
public:
    explicit KeychainSecretStore(const QString &serviceName);

    std::optional<QString> read(const QString &account) override;
    bool write(const QString &account, const QString &secret) override;
    bool remove(const QString &account) override;

private:
    QString serviceName;
};

#endif // KEYCHAINSECRETSTORE_H
