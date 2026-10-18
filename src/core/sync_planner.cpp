#include "core/sync_planner.h"

#include "core/path_utils.h"

#include <QDateTime>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>

namespace NutSync {

namespace {

constexpr qint64 kRemoteMtimeToleranceMs = 2000;

bool isDirectory(const LocalEntry &entry)
{
    return entry.type == EntryType::Directory;
}

bool isDirectory(const RemoteEntry &entry)
{
    return entry.type == EntryType::Directory;
}

SyncAction makeAction(ActionKind kind, const QString &path, const LocalEntry &local = {},
                      const RemoteEntry &remote = {}, const StateEntry &state = {},
                      bool stateExists = false)
{
    SyncAction action;
    action.kind = kind;
    action.path = path;
    action.local = local;
    action.remote = remote;
    action.state = state;
    action.stateExists = stateExists;
    return action;
}

bool hasActionUnder(const QList<SyncAction> &actions, const QString &prefix,
                    std::function<bool(const SyncAction &)> predicate)
{
    for (const SyncAction &action : actions) {
        if (PathUtils::isSameOrChild(action.path, prefix) && predicate(action))
            return true;
    }
    return false;
}

} // namespace

bool SyncPlanner::localUnchanged(const LocalEntry &local, const StateEntry &state)
{
    if (local.type != state.type)
        return false;
    if (local.type == EntryType::Directory)
        return true;
    return local.size == state.localSize && local.mtimeMs == state.localMtimeMs;
}

bool SyncPlanner::remoteUnchanged(const RemoteEntry &remote, const StateEntry &state)
{
    if (remote.type != state.type)
        return false;
    if (remote.type == EntryType::Directory)
        return true;

    const QString currentEtag = PathUtils::normalizeEtag(remote.etag);
    const QString knownEtag = PathUtils::normalizeEtag(state.remoteEtag);
    if (!currentEtag.isEmpty() && !knownEtag.isEmpty())
        return currentEtag == knownEtag;

    if (remote.size != state.remoteSize)
        return false;
    if (remote.mtimeMs > 0 && state.remoteMtimeMs > 0) {
        return std::llabs(remote.mtimeMs - state.remoteMtimeMs) <= kRemoteMtimeToleranceMs;
    }
    return true;
}

StateEntry SyncPlanner::makeState(const LocalEntry &local, const RemoteEntry &remote)
{
    StateEntry state;
    state.path = local.path;
    state.type = local.type;
    if (local.type == EntryType::File) {
        state.localSize = local.size;
        state.localMtimeMs = local.mtimeMs;
        state.remoteSize = remote.size;
        state.remoteMtimeMs = remote.mtimeMs;
        state.remoteEtag = PathUtils::normalizeEtag(remote.etag);
    }
    state.syncedAtMs = QDateTime::currentMSecsSinceEpoch();
    return state;
}

StateEntry SyncPlanner::makeDirectoryState(const QString &path)
{
    StateEntry state;
    state.path = path;
    state.type = EntryType::Directory;
    state.syncedAtMs = QDateTime::currentMSecsSinceEpoch();
    return state;
}

SyncPlan SyncPlanner::build(const QMap<QString, LocalEntry> &local,
                            const QMap<QString, RemoteEntry> &remote,
                            const QHash<QString, StateEntry> &state, bool syncLocalDeletions)
{
    SyncPlan plan;
    QSet<QString> conflicts;
    QSet<QString> removed;
    QSet<QString> actionKeys;
    QSet<QString> deletedPrefixes;

    auto addConflict = [&](const QString &path, const LocalEntry &localEntry,
                           const RemoteEntry &remoteEntry, const StateEntry &stateEntry,
                           bool stateExists) {
        if (conflicts.contains(path))
            return;
        conflicts.insert(path);
        plan.conflicts.append(path);
        plan.actions.append(makeAction(ActionKind::ResolveConflict, path, localEntry, remoteEntry,
                                       stateEntry, stateExists));
    };

    auto addUniqueAction = [&](const SyncAction &action) {
        const QString key =
            QString::number(static_cast<int>(action.kind)) + QLatin1Char('\x1f') + action.path;
        if (actionKeys.contains(key))
            return;
        actionKeys.insert(key);
        plan.actions.append(action);
    };

    QStringList keys;
    keys.reserve(local.size() + remote.size() + state.size());
    QSet<QString> keySet;
    for (auto it = local.cbegin(); it != local.cend(); ++it) {
        keys.append(it.key());
        keySet.insert(it.key());
    }
    for (auto it = remote.cbegin(); it != remote.cend(); ++it) {
        if (!keySet.contains(it.key())) {
            keys.append(it.key());
            keySet.insert(it.key());
        }
    }
    for (auto it = state.cbegin(); it != state.cend(); ++it) {
        if (!keySet.contains(it.key())) {
            keys.append(it.key());
            keySet.insert(it.key());
        }
    }
    std::sort(keys.begin(), keys.end());

    // Phase 1: files, deletions, and type conflicts.
    for (const QString &path : std::as_const(keys)) {
        const bool hasLocal = local.contains(path);
        const bool hasRemote = remote.contains(path);
        if (!hasLocal && !hasRemote) {
            if (state.contains(path))
                removed.insert(path);
            continue;
        }

        const LocalEntry localEntry = hasLocal ? local.value(path) : LocalEntry{};
        const RemoteEntry remoteEntry = hasRemote ? remote.value(path) : RemoteEntry{};
        const bool stateExists = state.contains(path);
        const StateEntry stateEntry = stateExists ? state.value(path) : StateEntry{};

        const bool localIsDir = hasLocal && isDirectory(localEntry);
        const bool remoteIsDir = hasRemote && isDirectory(remoteEntry);

        if (hasLocal && hasRemote && localIsDir && remoteIsDir)
            continue; // handled in directory phase

        if ((hasLocal && localIsDir) || (hasRemote && remoteIsDir)) {
            if (hasLocal && hasRemote) {
                addConflict(path, localEntry, remoteEntry, stateEntry, stateExists);
                continue;
            }
            if (hasLocal) {
                if (stateExists && stateEntry.type != EntryType::Directory)
                    addConflict(path, localEntry, remoteEntry, stateEntry, stateExists);
            } else {
                if (stateExists && stateEntry.type != EntryType::Directory)
                    addConflict(path, localEntry, remoteEntry, stateEntry, stateExists);
            }
            continue; // otherwise directory phase
        }

        // From here on all existing entries are files.
        if (hasLocal && hasRemote) {
            if (stateExists && stateEntry.type != EntryType::File) {
                addConflict(path, localEntry, remoteEntry, stateEntry, stateExists);
                continue;
            }
            if (!stateExists) {
                // Both sides have this file but there is no ancestor state. The
                // executor compares content first and records a baseline when equal.
                addConflict(path, localEntry, remoteEntry, stateEntry, stateExists);
                continue;
            }
            const bool localChanged = !localUnchanged(localEntry, stateEntry);
            const bool remoteChanged = !remoteUnchanged(remoteEntry, stateEntry);
            if (!localChanged && !remoteChanged)
                continue;
            if (localChanged && remoteChanged) {
                addConflict(path, localEntry, remoteEntry, stateEntry, stateExists);
            } else if (localChanged) {
                addUniqueAction(makeAction(ActionKind::Upload, path, localEntry, remoteEntry,
                                           stateEntry, stateExists));
            } else {
                addUniqueAction(makeAction(ActionKind::Download, path, localEntry, remoteEntry,
                                           stateEntry, stateExists));
            }
            continue;
        }

        if (hasLocal) {
            // Remote side is missing.
            if (stateExists && stateEntry.type != EntryType::File) {
                addConflict(path, localEntry, remoteEntry, stateEntry, stateExists);
                continue;
            }
            if (!stateExists) {
                addUniqueAction(makeAction(ActionKind::Upload, path, localEntry, {}, {}, false));
            } else if (localUnchanged(localEntry, stateEntry)) {
                addUniqueAction(
                    makeAction(ActionKind::DeleteLocal, path, localEntry, {}, stateEntry, true));
            } else {
                addConflict(path, localEntry, remoteEntry, stateEntry, stateExists);
            }
            continue;
        }

        // Local side is missing.
        if (stateExists && stateEntry.type != EntryType::File) {
            addConflict(path, localEntry, remoteEntry, stateEntry, stateExists);
            continue;
        }
        if (!stateExists) {
            addUniqueAction(makeAction(ActionKind::Download, path, {}, remoteEntry, {}, false));
        } else if (remoteUnchanged(remoteEntry, stateEntry)) {
            if (syncLocalDeletions) {
                addUniqueAction(
                    makeAction(ActionKind::DeleteRemote, path, {}, remoteEntry, stateEntry, true));
            } else {
                // Local deletion is not allowed to propagate. Downloading now
                // restores the remote copy during this synchronization.
                addUniqueAction(
                    makeAction(ActionKind::Download, path, {}, remoteEntry, stateEntry, true));
            }
        } else {
            addConflict(path, localEntry, remoteEntry, stateEntry, stateExists);
        }
    }

    auto canDeleteLocalSubtree = [&](const QString &prefix) {
        if (!local.contains(prefix) || !state.contains(prefix)
            || state.value(prefix).type != EntryType::Directory) {
            return false;
        }
        if (remote.contains(prefix))
            return false;
        for (auto it = local.cbegin(); it != local.cend(); ++it) {
            if (!PathUtils::isSameOrChild(it.key(), prefix))
                continue;
            if (!state.contains(it.key()))
                return false;
            const StateEntry &entryState = state.value(it.key());
            if (it.value().type != entryState.type)
                return false;
            if (it.value().type == EntryType::File && !localUnchanged(it.value(), entryState)) {
                return false;
            }
        }
        for (auto it = remote.cbegin(); it != remote.cend(); ++it) {
            if (PathUtils::isSameOrChild(it.key(), prefix))
                return false;
        }
        return !hasActionUnder(plan.actions, prefix, [](const SyncAction &action) {
            return action.kind != ActionKind::DeleteLocal;
        });
    };

    auto canDeleteRemoteSubtree = [&](const QString &prefix) {
        if (!remote.contains(prefix) || !state.contains(prefix)
            || state.value(prefix).type != EntryType::Directory) {
            return false;
        }
        if (local.contains(prefix))
            return false;
        for (auto it = remote.cbegin(); it != remote.cend(); ++it) {
            if (!PathUtils::isSameOrChild(it.key(), prefix))
                continue;
            if (!state.contains(it.key()))
                return false;
            const StateEntry &entryState = state.value(it.key());
            if (it.value().type != entryState.type)
                return false;
            if (it.value().type == EntryType::File && !remoteUnchanged(it.value(), entryState)) {
                return false;
            }
        }
        for (auto it = local.cbegin(); it != local.cend(); ++it) {
            if (PathUtils::isSameOrChild(it.key(), prefix))
                return false;
        }
        return !hasActionUnder(plan.actions, prefix, [](const SyncAction &action) {
            return action.kind != ActionKind::DeleteRemote;
        });
    };

    auto removeActionsUnder = [&](const QString &prefix) {
        QList<SyncAction> kept;
        for (const SyncAction &action : std::as_const(plan.actions)) {
            if (!PathUtils::isSameOrChild(action.path, prefix))
                kept.append(action);
        }
        plan.actions = std::move(kept);
    };

    // Phase 2: directory deletion propagation and creation.
    const QStringList originalKeys = keys;
    for (const QString &path : originalKeys) {
        if (deletedPrefixes.contains(path))
            continue;
        bool underDeleted = false;
        for (const QString &prefix : std::as_const(deletedPrefixes)) {
            if (PathUtils::isSameOrChild(path, prefix)) {
                underDeleted = true;
                break;
            }
        }
        if (underDeleted)
            continue;

        const bool hasLocal = local.contains(path);
        const bool hasRemote = remote.contains(path);
        const bool localDir = hasLocal && isDirectory(local.value(path));
        const bool remoteDir = hasRemote && isDirectory(remote.value(path));
        if (!localDir && !remoteDir)
            continue;
        if (conflicts.contains(path))
            continue;

        const bool stateExists = state.contains(path);
        const StateEntry stateEntry = stateExists ? state.value(path) : StateEntry{};

        if (localDir && remoteDir) {
            if (!stateExists || stateEntry.type != EntryType::Directory)
                plan.baselineUpdates.append(makeDirectoryState(path));
            continue;
        }

        if (localDir) {
            if (stateExists && stateEntry.type != EntryType::Directory) {
                addConflict(path, local.value(path), {}, stateEntry, true);
                continue;
            }
            if (stateExists && canDeleteLocalSubtree(path)) {
                removeActionsUnder(path);
                addUniqueAction(makeAction(ActionKind::DeleteLocal, path, local.value(path)));
                deletedPrefixes.insert(path);
                for (const QString &key : std::as_const(keys)) {
                    if (key != path && PathUtils::isSameOrChild(key, path))
                        deletedPrefixes.insert(key);
                }
                continue;
            }
            addUniqueAction(makeAction(ActionKind::CreateRemoteDir, path, local.value(path)));
            continue;
        }

        // Remote directory only.
        if (stateExists && stateEntry.type != EntryType::Directory) {
            addConflict(path, {}, remote.value(path), stateEntry, true);
            continue;
        }
        if (syncLocalDeletions && stateExists && canDeleteRemoteSubtree(path)) {
            removeActionsUnder(path);
            addUniqueAction(makeAction(ActionKind::DeleteRemote, path, {}, remote.value(path)));
            deletedPrefixes.insert(path);
            for (const QString &key : std::as_const(keys)) {
                if (key != path && PathUtils::isSameOrChild(key, path))
                    deletedPrefixes.insert(key);
            }
            continue;
        }
        addUniqueAction(makeAction(ActionKind::CreateLocalDir, path, {}, remote.value(path)));
    }

    auto keepBaselineOutside = [&](const QString &prefix) {
        QList<StateEntry> kept;
        for (const StateEntry &entry : std::as_const(plan.baselineUpdates)) {
            if (!PathUtils::isSameOrChild(entry.path, prefix))
                kept.append(entry);
        }
        plan.baselineUpdates = std::move(kept);
    };
    for (const QString &prefix : std::as_const(deletedPrefixes))
        keepBaselineOutside(prefix);

    std::sort(plan.actions.begin(), plan.actions.end(),
              [](const SyncAction &left, const SyncAction &right) {
                  auto rank = [](ActionKind kind) {
                      switch (kind) {
                      case ActionKind::CreateRemoteDir:
                          return 0;
                      case ActionKind::CreateLocalDir:
                          return 1;
                      case ActionKind::ResolveConflict:
                          return 2;
                      case ActionKind::Upload:
                          return 3;
                      case ActionKind::Download:
                          return 4;
                      case ActionKind::DeleteRemote:
                          return 5;
                      case ActionKind::DeleteLocal:
                          return 6;
                      }
                      return 7;
                  };
                  const int leftRank = rank(left.kind);
                  const int rightRank = rank(right.kind);
                  if (leftRank != rightRank)
                      return leftRank < rightRank;
                  const bool leftCreate = leftRank <= 1;
                  const bool rightCreate = rightRank <= 1;
                  if (leftCreate && rightCreate) {
                      const int leftDepth = PathUtils::pathDepth(left.path);
                      const int rightDepth = PathUtils::pathDepth(right.path);
                      if (leftDepth != rightDepth)
                          return leftDepth < rightDepth;
                  }
                  const bool leftDelete = leftRank >= 5;
                  const bool rightDelete = rightRank >= 5;
                  if (leftDelete && rightDelete) {
                      const int leftDepth = PathUtils::pathDepth(left.path);
                      const int rightDepth = PathUtils::pathDepth(right.path);
                      if (leftDepth != rightDepth)
                          return leftDepth > rightDepth;
                  }
                  return left.path < right.path;
              });

    for (const SyncAction &action : std::as_const(plan.actions)) {
        switch (action.kind) {
        case ActionKind::Upload:
            ++plan.uploads;
            break;
        case ActionKind::Download:
            ++plan.downloads;
            break;
        case ActionKind::DeleteLocal:
        case ActionKind::DeleteRemote:
            ++plan.deletes;
            break;
        default:
            break;
        }
    }
    for (const QString &path : std::as_const(removed))
        plan.removedStates.append(path);
    plan.removedStates.sort();
    plan.removedStates.removeDuplicates();
    plan.conflicts.sort();
    plan.conflicts.removeDuplicates();
    return plan;
}

} // namespace NutSync
