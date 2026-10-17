#pragma once

#include "core/sync_types.h"

#include <QHash>
#include <QMap>
#include <QStringList>

namespace NutSync {

class SyncPlanner
{
public:
    static SyncPlan build(const QMap<QString, LocalEntry> &local,
                          const QMap<QString, RemoteEntry> &remote,
                          const QHash<QString, StateEntry> &state,
                          bool syncLocalDeletions = true);

    static bool localUnchanged(const LocalEntry &local, const StateEntry &state);
    static bool remoteUnchanged(const RemoteEntry &remote, const StateEntry &state);
    static StateEntry makeState(const LocalEntry &local, const RemoteEntry &remote);
    static StateEntry makeDirectoryState(const QString &path);
};

} // namespace NutSync
