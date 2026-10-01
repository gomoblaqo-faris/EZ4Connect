#include "application/profilesettings.h"

#include <QByteArray>
#include <QDebug>
#include <QRegularExpression>
#include <QUrl>
#include <QUuid>

#include <iterator>

#include "application/secretstore.h"

namespace
{
SecretStore *activeStore = nullptr;
QString pendingRemovalsFile;

const ProfileSettings::SecretKey *const allSecrets[] = {
    &ProfileSettings::Password,
    &ProfileSettings::CertPassword,
    &ProfileSettings::TOTPSecret,
    &ProfileSettings::ShadowsocksURL,
};

const char *const pendingRemovalsKey = "Secrets/PendingRemoval";

// Removals are remembered per store entry, not per profile: retrying a whole
// profile would also delete its secrets that are still in use.
void rememberForRetry(const QString &storeAccount)
{
    if (pendingRemovalsFile.isEmpty())
    {
        return;
    }
    QSettings state(pendingRemovalsFile, QSettings::IniFormat);
    QStringList pending = state.value(pendingRemovalsKey).toStringList();
    if (!pending.contains(storeAccount))
    {
        pending << storeAccount;
        state.setValue(pendingRemovalsKey, pending);
        state.sync();
    }
}

// An entry that has been written again is in use, so an older request to
// remove it must not be carried out.
void noLongerPending(const QString &storeAccount)
{
    if (pendingRemovalsFile.isEmpty())
    {
        return;
    }
    QSettings state(pendingRemovalsFile, QSettings::IniFormat);
    QStringList pending = state.value(pendingRemovalsKey).toStringList();
    if (pending.removeAll(storeAccount) == 0)
    {
        return;
    }
    if (pending.isEmpty())
    {
        state.remove(pendingRemovalsKey);
    }
    else
    {
        state.setValue(pendingRemovalsKey, pending);
    }
    state.sync();
}

bool removeFromStore(const QString &storeAccount, const char *settingName)
{
    if (activeStore->remove(storeAccount))
    {
        return true;
    }
    qWarning().noquote()
        << "Could not remove" << settingName << "from the system credential store";
    rememberForRetry(storeAccount);
    return false;
}

QString account(const QString &secretId, const ProfileSettings::SecretKey &key)
{
    return secretId + "/" + key.name;
}

QString readFromFile(const QSettings &settings, const ProfileSettings::SecretKey &key)
{
    const QString stored = settings.value(key.name).toString();
    return key.base64InFile
        ? QString::fromUtf8(QByteArray::fromBase64(stored.toUtf8()))
        : stored;
}

void writeToFile(QSettings &settings, const ProfileSettings::SecretKey &key, const QString &secret)
{
    settings.setValue(
        key.name,
        key.base64InFile ? QString::fromLatin1(secret.toUtf8().toBase64()) : secret
    );
}
}

namespace
{
bool removeAllFromStore(const QString &secretId)
{
    bool allRemoved = true;
    for (const ProfileSettings::SecretKey *key : allSecrets)
    {
        if (!removeFromStore(account(secretId, *key), key->name))
        {
            allRemoved = false;
        }
    }
    return allRemoved;
}
}

void ProfileSettings::setSecretStore(SecretStore *store)
{
    activeStore = store;
}

void ProfileSettings::setPendingRemovalsFile(const QString &path)
{
    pendingRemovalsFile = path;
}

void ProfileSettings::retryPendingSecretRemovals()
{
    if (activeStore == nullptr || pendingRemovalsFile.isEmpty())
    {
        return;
    }

    QSettings state(pendingRemovalsFile, QSettings::IniFormat);
    const QStringList pending = state.value(pendingRemovalsKey).toStringList();
    QStringList stillPending;
    for (const QString &storeAccount : pending)
    {
        if (!activeStore->remove(storeAccount))
        {
            stillPending << storeAccount;
        }
    }
    if (stillPending != pending)
    {
        if (stillPending.isEmpty())
        {
            state.remove(pendingRemovalsKey);
        }
        else
        {
            state.setValue(pendingRemovalsKey, stillPending);
        }
        state.sync();
    }
}

bool ProfileSettings::usesSecretStore()
{
    return activeStore != nullptr;
}

// A secret is either in the profile file or in the store, never both: while
// its key is present in the file, the file is what counts. That keeps
// profiles written by versions without a store, and secrets the store would
// not take, working unchanged.
QString ProfileSettings::read(const QSettings &settings, const SecretKey &key)
{
    if (settings.contains(key.name) || activeStore == nullptr)
    {
        return readFromFile(settings, key);
    }

    const QString secretId = read(settings, SecretId);
    if (secretId.isEmpty())
    {
        return {};
    }
    return activeStore->read(account(secretId, key)).value_or(QString());
}

void ProfileSettings::write(QSettings &settings, const SecretKey &key, const QString &secret)
{
    if (activeStore == nullptr)
    {
        writeToFile(settings, key, secret);
        return;
    }

    QString secretId = read(settings, SecretId);
    if (secret.isEmpty())
    {
        // Nothing to protect. Keep the empty key, as a fresh profile has it.
        // If the entry cannot be removed now it is hidden by that key, and
        // its removal is tried again later.
        if (!secretId.isEmpty())
        {
            removeFromStore(account(secretId, key), key.name);
        }
        writeToFile(settings, key, QString());
        settings.sync();
        return;
    }

    // Each step is saved before the next, so that stopping at any point
    // leaves either the old value or the new one readable: the identifier
    // before the entry that needs it, and the entry before the file key that
    // would otherwise hide it is dropped.
    if (secretId.isEmpty())
    {
        secretId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        write(settings, SecretId, secretId);
        settings.sync();
    }
    if (activeStore->write(account(secretId, key), secret))
    {
        noLongerPending(account(secretId, key));
        settings.remove(key.name);
        settings.sync();
        return;
    }

    qWarning().noquote()
        << "Could not save" << key.name
        << "to the system credential store; keeping it in the profile file";
    writeToFile(settings, key, secret);
}

void ProfileSettings::writeIfChanged(
    QSettings &settings,
    const SecretKey &key,
    const QString &loaded,
    const QString &current
)
{
    if (current != loaded)
    {
        write(settings, key, current);
    }
}

void ProfileSettings::migrateSecrets(QSettings &settings)
{
    if (activeStore == nullptr)
    {
        return;
    }

    for (const SecretKey *key : allSecrets)
    {
        if (!settings.contains(key->name))
        {
            continue;
        }
        const QString secret = readFromFile(settings, *key);
        if (!secret.isEmpty())
        {
            write(settings, *key, secret);
        }
    }
}

bool ProfileSettings::forgetSecrets(const QString &secretId)
{
    if (activeStore == nullptr || secretId.isEmpty())
    {
        return true;
    }

    // The profile that named these entries is about to lose the identifier.
    // Entries that cannot be removed now are remembered and tried again.
    return removeAllFromStore(secretId);
}

bool ProfileSettings::forgetSecrets(QSettings &settings)
{
    const bool allRemoved = forgetSecrets(read(settings, SecretId));
    settings.remove(SecretId.name);
    return allRemoved;
}

void ProfileSettings::stripSecrets(QSettings &exportedCopy)
{
    for (const SecretKey *key : allSecrets)
    {
        exportedCopy.remove(key->name);
    }
    exportedCopy.remove(SecretId.name);
    if (read(exportedCopy, DialDirectProxy).contains('@'))
    {
        exportedCopy.remove(DialDirectProxy.name);
    }

    // Free-form settings can carry credentials too: an extra "-password x",
    // or a login URL with a ticket in its query.
    static const QRegularExpression secretLike(
        "(^|[^a-z])(password|passwd|secret|token|ticket|twf-?id|sid)([^a-z]|$)",
        QRegularExpression::CaseInsensitiveOption
    );
    if (secretLike.match(read(exportedCopy, ExtraArguments)).hasMatch())
    {
        exportedCopy.remove(ExtraArguments.name);
    }
    const QString loginUrl = read(exportedCopy, LoginURL);
    if (secretLike.match(loginUrl).hasMatch() || loginUrl.contains('@'))
    {
        write(
            exportedCopy,
            LoginURL,
            QUrl(loginUrl).toString(
                QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo
            )
        );
    }
}

void ProfileSettings::detachSecrets(QSettings &settings)
{
    const QString sourceId = read(settings, SecretId);
    if (activeStore == nullptr || sourceId.isEmpty())
    {
        return;
    }

    // Copy before dropping the shared identifier, which read() depends on.
    // A secret that cannot be read is simply not carried over: the copy then
    // asks for it again, and the original profile is unaffected. Keeping the
    // shared identifier instead would let either profile overwrite or
    // delete the other's secrets.
    QString copies[std::size(allSecrets)];
    for (std::size_t index = 0; index < std::size(allSecrets); ++index)
    {
        if (!settings.contains(allSecrets[index]->name))
        {
            copies[index] = read(settings, *allSecrets[index]);
        }
    }

    settings.remove(SecretId.name);
    for (std::size_t index = 0; index < std::size(allSecrets); ++index)
    {
        if (!copies[index].isEmpty())
        {
            write(settings, *allSecrets[index], copies[index]);
        }
    }
}
