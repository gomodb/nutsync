#pragma once

#include "core/sync_types.h"

#include <QString>
#include <QUrl>

#include <functional>
#include <utility>

namespace NutSync {

using CancelledCallback = std::function<bool()>;
using TransferProgressCallback = std::function<void(qint64 received, qint64 total)>;

struct DownloadResult
{
    qint64 size = 0;
    qint64 mtimeMs = 0;
    QString etag;
};

/**
 * Protocol-independent remote side of a sync pair.
 *
 * Paths passed to this interface are normalized, relative, UTF-8 paths using
 * '/' as separator. Implementations are synchronous and run in a worker
 * thread. Adding another protocol later only requires another implementation.
 */
class RemoteBackend
{
public:
    virtual ~RemoteBackend() = default;

    virtual QString typeName() const = 0;
    virtual QString endpointDescription() const = 0;
    virtual RemoteErrorClass lastErrorClass() const { return RemoteErrorClass::Other; }

    virtual bool testConnection(QString *error) = 0;
    virtual bool scan(QList<RemoteEntry> *entries, QString *error,
                      const CancelledCallback &cancelled) = 0;
    virtual bool stat(const QString &path, RemoteEntry *entry, bool *exists, QString *error) = 0;
    virtual bool download(const QString &path, const QString &localFile, DownloadResult *result,
                          QString *error, const CancelledCallback &cancelled) = 0;
    virtual bool upload(const QString &localFile, const QString &path, const QString &expectedEtag,
                        QString *error, const CancelledCallback &cancelled) = 0;
    virtual bool makeDirectory(const QString &path, QString *error) = 0;
    virtual bool remove(const QString &path, bool recursive, const QString &expectedEtag,
                        QString *error, const CancelledCallback &cancelled) = 0;

    void setTransferProgress(TransferProgressCallback callback)
    {
        m_transferProgress = std::move(callback);
    }

protected:
    void reportTransferProgress(qint64 received, qint64 total) const
    {
        if (m_transferProgress)
            m_transferProgress(received, total);
    }

private:
    TransferProgressCallback m_transferProgress;
};

} // namespace NutSync
