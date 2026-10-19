#include "core/sync_types.h"

#include <QObject>

namespace NutSync {

QString syncModeToString(SyncMode mode)
{
    switch (mode) {
    case SyncMode::LocalToRemote:
        return QStringLiteral("local-to-remote");
    case SyncMode::RemoteToLocal:
        return QStringLiteral("remote-to-local");
    case SyncMode::Bidirectional:
        break;
    }
    return QStringLiteral("bidirectional");
}

SyncMode syncModeFromString(const QString &value)
{
    if (value == QLatin1String("local-to-remote"))
        return SyncMode::LocalToRemote;
    if (value == QLatin1String("remote-to-local"))
        return SyncMode::RemoteToLocal;
    return SyncMode::Bidirectional;
}

QString syncModeDisplayName(SyncMode mode)
{
    switch (mode) {
    case SyncMode::LocalToRemote:
        return QObject::tr("仅本地到远程");
    case SyncMode::RemoteToLocal:
        return QObject::tr("仅远程到本地");
    case SyncMode::Bidirectional:
        break;
    }
    return QObject::tr("双向同步");
}

QString conflictPolicyToString(ConflictPolicy policy)
{
    switch (policy) {
    case ConflictPolicy::KeepBoth:
        return QStringLiteral("keep-both");
    case ConflictPolicy::PreferLocal:
        return QStringLiteral("prefer-local");
    case ConflictPolicy::PreferRemote:
        return QStringLiteral("prefer-remote");
    case ConflictPolicy::NewerWins:
        return QStringLiteral("newer-wins");
    }
    return QStringLiteral("keep-both");
}

ConflictPolicy conflictPolicyFromString(const QString &value)
{
    if (value == QLatin1String("prefer-local"))
        return ConflictPolicy::PreferLocal;
    if (value == QLatin1String("prefer-remote"))
        return ConflictPolicy::PreferRemote;
    if (value == QLatin1String("newer-wins"))
        return ConflictPolicy::NewerWins;
    return ConflictPolicy::KeepBoth;
}

QString conflictPolicyDisplayName(ConflictPolicy policy)
{
    switch (policy) {
    case ConflictPolicy::KeepBoth:
        return QObject::tr("保留双方副本（推荐）");
    case ConflictPolicy::PreferLocal:
        return QObject::tr("本地优先");
    case ConflictPolicy::PreferRemote:
        return QObject::tr("远程优先");
    case ConflictPolicy::NewerWins:
        return QObject::tr("较新者优先");
    }
    return {};
}

QString rateLimitWindowDisplay(int windowMinutes)
{
    if (windowMinutes > 0 && windowMinutes % 60 == 0)
        return QObject::tr("%1 小时").arg(windowMinutes / 60);
    return QObject::tr("%1 分钟").arg(windowMinutes);
}

QString rateLimitDescription(bool enabled, int windowMinutes, int maxRequests)
{
    if (!enabled || windowMinutes <= 0 || maxRequests <= 0)
        return QObject::tr("不限制");
    return QObject::tr("每 %1内最多 %2 次")
        .arg(rateLimitWindowDisplay(windowMinutes))
        .arg(maxRequests);
}

QString actionKindToString(ActionKind kind)
{
    switch (kind) {
    case ActionKind::Upload:
        return QStringLiteral("upload");
    case ActionKind::Download:
        return QStringLiteral("download");
    case ActionKind::CreateRemoteDir:
        return QStringLiteral("create-remote-dir");
    case ActionKind::CreateLocalDir:
        return QStringLiteral("create-local-dir");
    case ActionKind::DeleteRemote:
        return QStringLiteral("delete-remote");
    case ActionKind::DeleteLocal:
        return QStringLiteral("delete-local");
    case ActionKind::ResolveConflict:
        return QStringLiteral("resolve-conflict");
    case ActionKind::MoveRemote:
        return QStringLiteral("move-remote");
    }
    return QStringLiteral("unknown");
}

} // namespace NutSync
