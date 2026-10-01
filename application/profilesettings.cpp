#include "application/profilesettings.h"

#include <QByteArray>
#include <QDebug>
#include <QUuid>

#include <iterator>

#include "application/secretstore.h"

namespace
{
SecretStore *activeStore = nullptr;

const ProfileSettings::SecretKey *const allSecrets[] = {
    &ProfileSettings::Password,
    &ProfileSettings::CertPassword,
    &ProfileSettings::TOTPSecret,
};

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

void ProfileSettings::setSecretStore(SecretStore *store)
{
    activeStore = store;
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
        // If the entry cannot be removed it is merely hidden by that key, and
        // the identifier stays so a later cleanup can still find it.
        if (!secretId.isEmpty() && !activeStore->remove(account(secretId, key)))
        {
            qWarning().noquote()
                << "Could not remove" << key.name << "from the system credential store";
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

    bool allRemoved = true;
    for (const SecretKey *key : allSecrets)
    {
        if (!activeStore->remove(account(secretId, *key)))
        {
            qWarning().noquote()
                << "Could not remove" << key->name << "from the system credential store";
            allRemoved = false;
        }
    }
    return allRemoved;
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
