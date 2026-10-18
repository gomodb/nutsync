#pragma once

#include "core/remote_backend.h"

#include <QDateTime>
#include <QList>
#include <QString>

namespace NutSync::TrashManager {

struct TrashEntry
{
    QString absolutePath;
    QString relativePath;
    bool directory = false;
    qint64 size = 0;
    QDateTime modified;
};

QList<TrashEntry> listEntries(const QString &pairId);
bool restoreEntry(const TrashEntry &entry, const QString &localRoot, bool overwrite,
                  QString *error = nullptr);

QString pairDir(const QString &pairId);
QString createBatch(const QString &pairId, int retentionDays, QString *error = nullptr);
bool copyLocalPath(const QString &source, const QString &batchRoot, const QString &relativePath,
                   QString *error = nullptr);
bool downloadRemoteFile(RemoteBackend &backend, const QString &remotePath, const QString &batchRoot,
                        const QString &relativePath, const CancelledCallback &cancelled,
                        QString *error = nullptr);
void cleanupOldBatches(const QString &pairId, int retentionDays);

} // namespace NutSync::TrashManager
