#pragma once

#include "core/remote_backend.h"

#include <QString>

namespace NutSync::TrashManager {

QString pairDir(const QString &pairId);
QString createBatch(const QString &pairId, int retentionDays, QString *error = nullptr);
bool copyLocalPath(const QString &source, const QString &batchRoot, const QString &relativePath,
                   QString *error = nullptr);
bool downloadRemoteFile(RemoteBackend &backend, const QString &remotePath, const QString &batchRoot,
                        const QString &relativePath, const CancelledCallback &cancelled,
                        QString *error = nullptr);
void cleanupOldBatches(const QString &pairId, int retentionDays);

} // namespace NutSync::TrashManager
