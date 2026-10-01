#include "keychainsecretstore.h"

#include <QDebug>
#include <QEventLoop>
#include <QTimer>

#include <memory>
#include <type_traits>

#include <qt6keychain/keychain.h>

namespace
{
// Long enough to answer a prompt from the operating system, short enough
// that a credential service that hangs does not hang the application.
const int storeTimeoutMs = 60000;

struct JobResult
{
    bool finished = false;
    QKeychain::Error error = QKeychain::OtherError;
    QString errorString = "the system credential store did not answer in time";
    QString text;
    QEventLoop *loop = nullptr;
};

// QtKeychain jobs only finish from the event loop, and callers need the
// result before they can continue, so this waits for it. On macOS the result
// arrives through the main dispatch queue, which only a GUI application's
// event loop drains: under a plain QCoreApplication this would time out.
//
// The job deletes itself when it is done, which can be after this function
// has given up on it, so what it reports is copied into a result that both
// sides share instead of being read from the job afterwards.
template <typename Job>
JobResult runToCompletion(Job *job)
{
    auto result = std::make_shared<JobResult>();
    QEventLoop loop;
    result->loop = &loop;

    QObject::connect(job, &QKeychain::Job::finished, job, [result, job]()
    {
        result->finished = true;
        result->error = job->error();
        result->errorString = job->errorString();
        if constexpr (std::is_same_v<Job, QKeychain::ReadPasswordJob>)
        {
            result->text = job->textData();
        }
        if (result->loop != nullptr)
        {
            result->loop->quit();
        }
    });
    QTimer::singleShot(storeTimeoutMs, &loop, &QEventLoop::quit);

    job->start();
    // Callers are in the middle of reading or writing a profile. Letting the
    // user switch profile or close a dialog meanwhile would pull the settings
    // out from under them.
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    result->loop = nullptr;
    return *result;
}
}

KeychainSecretStore::KeychainSecretStore(const QString &serviceName)
    : serviceName(serviceName)
{
}

std::optional<QString> KeychainSecretStore::read(const QString &account)
{
    auto *job = new QKeychain::ReadPasswordJob(serviceName);
    job->setKey(account);
    const JobResult result = runToCompletion(job);

    if (result.finished && result.error == QKeychain::NoError)
    {
        return result.text;
    }
    if (!result.finished || result.error != QKeychain::EntryNotFound)
    {
        qWarning().noquote() << "Could not read from the system credential store:" << result.errorString;
    }
    return std::nullopt;
}

bool KeychainSecretStore::write(const QString &account, const QString &secret)
{
    auto *job = new QKeychain::WritePasswordJob(serviceName);
    job->setKey(account);
    job->setTextData(secret);
    const JobResult result = runToCompletion(job);

    if (!result.finished || result.error != QKeychain::NoError)
    {
        qWarning().noquote() << "Could not write to the system credential store:" << result.errorString;
        return false;
    }
    return true;
}

bool KeychainSecretStore::remove(const QString &account)
{
    auto *job = new QKeychain::DeletePasswordJob(serviceName);
    job->setKey(account);
    const JobResult result = runToCompletion(job);

    if (!result.finished
        || (result.error != QKeychain::NoError && result.error != QKeychain::EntryNotFound))
    {
        qWarning().noquote() << "Could not remove from the system credential store:" << result.errorString;
        return false;
    }
    return true;
}
