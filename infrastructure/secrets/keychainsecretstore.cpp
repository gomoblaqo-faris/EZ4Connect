#include "keychainsecretstore.h"

#include <QDebug>
#include <QEventLoop>

#include <qt6keychain/keychain.h>

namespace
{
// QtKeychain jobs only finish from the event loop. Callers need the result
// before they can continue, so wait for it here. On macOS the result arrives
// through the main dispatch queue, which only a GUI application's event loop
// drains: under a plain QCoreApplication this would wait forever.
void runToCompletion(QKeychain::Job &job)
{
    job.setAutoDelete(false);
    QEventLoop loop;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job.start();
    // Callers are in the middle of reading or writing a profile. Letting the
    // user switch profile or close a dialog meanwhile would pull the settings
    // out from under them.
    loop.exec(QEventLoop::ExcludeUserInputEvents);
}
}

KeychainSecretStore::KeychainSecretStore(const QString &serviceName)
    : serviceName(serviceName)
{
}

std::optional<QString> KeychainSecretStore::read(const QString &account)
{
    QKeychain::ReadPasswordJob job(serviceName);
    job.setKey(account);
    runToCompletion(job);

    if (job.error() == QKeychain::NoError)
    {
        return job.textData();
    }
    if (job.error() != QKeychain::EntryNotFound)
    {
        qWarning().noquote() << "Could not read from the system credential store:" << job.errorString();
    }
    return std::nullopt;
}

bool KeychainSecretStore::write(const QString &account, const QString &secret)
{
    QKeychain::WritePasswordJob job(serviceName);
    job.setKey(account);
    job.setTextData(secret);
    runToCompletion(job);

    if (job.error() != QKeychain::NoError)
    {
        qWarning().noquote() << "Could not write to the system credential store:" << job.errorString();
        return false;
    }
    return true;
}

bool KeychainSecretStore::remove(const QString &account)
{
    QKeychain::DeletePasswordJob job(serviceName);
    job.setKey(account);
    runToCompletion(job);

    if (job.error() != QKeychain::NoError && job.error() != QKeychain::EntryNotFound)
    {
        qWarning().noquote() << "Could not remove from the system credential store:" << job.errorString();
        return false;
    }
    return true;
}
