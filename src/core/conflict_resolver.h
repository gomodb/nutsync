#pragma once

#include "core/remote_backend.h"
#include "core/sync_types.h"

#include <QString>

namespace NutSync {

enum class ConflictResolution {
    KeepLocal,
    KeepRemote,
};

struct ConflictResolutionResult
{
    bool success = false;
    QString error;
};

ConflictResolutionResult resolveConflict(const SyncPair &pair, const ConflictRecord &record,
                                         ConflictResolution resolution, RemoteBackend &backend,
                                         const QString &stateDatabasePath);
ConflictResolutionResult resolveTypeConflict(const SyncPair &pair, const ConflictRecord &record,
                                             ConflictResolution resolution, RemoteBackend &backend,
                                             const QString &stateDatabasePath);

} // namespace NutSync
