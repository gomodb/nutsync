#include "core/credential_store.h"

#include <QEventLoop>
#include <QObject>

#ifdef NUTSYNC_HAVE_KEYCHAIN
#if __has_include(<qt6keychain/keychain.h>)
#include <qt6keychain/keychain.h>
#else
#include <keychain.h>
#endif
#endif

namespace NutSync {

namespace {
#ifdef NUTSYNC_HAVE_KEYCHAIN
template<typename Job>
bool runJob(Job &job, QString *error)
{
    QEventLoop loop;
    bool finished = false;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, [&](QKeychain::Job *) {
        finished = true;
        loop.quit();
    });
    job.setAutoDelete(false);
    job.start();
    if (!finished)
        loop.exec();
    if (job.error() != QKeychain::NoError) {
        if (error)
            *error = job.errorString();
        return false;
    }
    return true;
}
QString readPasswordFromService(const QString &service, const QString &key,
                                bool *found, QString *error)
{
    QKeychain::ReadPasswordJob job(service);
    job.setKey(key);
    if (!runJob(job, error)) {
        if (job.error() == QKeychain::EntryNotFound) {
            if (found)
                *found = false;
            if (error)
                error->clear();
            return {};
        }
        if (found)
            *found = false;
        return {};
    }
    if (found)
        *found = true;
    return job.textData();
}
#endif
} // namespace

CredentialStore::CredentialStore()
    : m_service(serviceName())
{
}

QString CredentialStore::serviceName()
{
    return QStringLiteral("io.github.gomodb.NutSync");
}

QString CredentialStore::legacyServiceName()
{
    return QStringLiteral("io.github.nutsync.NutSync");
}

bool CredentialStore::isAvailable() const
{
#ifdef NUTSYNC_HAVE_KEYCHAIN
    return true;
#else
    return false;
#endif
}

QString CredentialStore::readPassword(const QString &key, QString *error) const
{
#ifdef NUTSYNC_HAVE_KEYCHAIN
    bool found = false;
    const QString password = readPasswordFromService(m_service, key, &found, error);
    if (found)
        return password;
    if (error && !error->isEmpty())
        return {};

    QString legacyError;
    bool legacyFound = false;
    const QString legacyPassword = readPasswordFromService(
        legacyServiceName(), key, &legacyFound, &legacyError);
    if (legacyFound) {
        if (!legacyPassword.isEmpty()) {
            QString migrationError;
            writePassword(key, legacyPassword, &migrationError);
        }
        return legacyPassword;
    }
    if (error)
        *error = legacyError;
    return {};
#else
    Q_UNUSED(key)
    if (error)
        *error = QObject::tr("当前构建未启用系统钥匙串，密码不会被保存");
    return {};
#endif
}

bool CredentialStore::writePassword(const QString &key, const QString &password,
                                    QString *error) const
{
#ifdef NUTSYNC_HAVE_KEYCHAIN
    QKeychain::WritePasswordJob job(m_service);
    job.setKey(key);
    job.setTextData(password);
    return runJob(job, error);
#else
    Q_UNUSED(key)
    Q_UNUSED(password)
    if (error)
        *error = QObject::tr("当前构建未启用系统钥匙串，密码不会被保存");
    return false;
#endif
}

bool CredentialStore::deletePassword(const QString &key, QString *error) const
{
#ifdef NUTSYNC_HAVE_KEYCHAIN
    QKeychain::DeletePasswordJob job(m_service);
    job.setKey(key);
    if (!runJob(job, error)) {
        // Deleting a non-existent entry is not an error for our purposes.
        if (job.error() != QKeychain::EntryNotFound)
            return false;
        if (error)
            error->clear();
    }

    QKeychain::DeletePasswordJob legacyJob(legacyServiceName());
    legacyJob.setKey(key);
    if (!runJob(legacyJob, nullptr) && legacyJob.error() != QKeychain::EntryNotFound)
        return false;
    return true;
#else
    Q_UNUSED(key)
    if (error)
        *error = QObject::tr("当前构建未启用系统钥匙串");
    return false;
#endif
}

} // namespace NutSync
