#include "test_sync_planner.h"

#include "core/sync_planner.h"

#include <QTest>

namespace NutSync::Test {

namespace {

LocalEntry localFile(const QString &path, qint64 size = 4, qint64 mtime = 1000)
{
    LocalEntry entry;
    entry.path = path;
    entry.type = EntryType::File;
    entry.size = size;
    entry.mtimeMs = mtime;
    return entry;
}

LocalEntry localDir(const QString &path)
{
    LocalEntry entry;
    entry.path = path;
    entry.type = EntryType::Directory;
    entry.mtimeMs = 1000;
    return entry;
}

RemoteEntry remoteFile(const QString &path, const QString &etag, qint64 size = 4,
                       qint64 mtime = 1000)
{
    RemoteEntry entry;
    entry.path = path;
    entry.type = EntryType::File;
    entry.size = size;
    entry.mtimeMs = mtime;
    entry.etag = etag;
    return entry;
}

RemoteEntry remoteDir(const QString &path)
{
    RemoteEntry entry;
    entry.path = path;
    entry.type = EntryType::Directory;
    entry.mtimeMs = 1000;
    return entry;
}

StateEntry fileState(const QString &path, qint64 localSize = 4, qint64 localMtime = 1000,
                     qint64 remoteSize = 4, qint64 remoteMtime = 1000,
                     const QString &etag = QStringLiteral("\"e1\""))
{
    StateEntry state;
    state.path = path;
    state.type = EntryType::File;
    state.localSize = localSize;
    state.localMtimeMs = localMtime;
    state.remoteSize = remoteSize;
    state.remoteMtimeMs = remoteMtime;
    state.remoteEtag = etag;
    state.syncedAtMs = 1234;
    return state;
}

StateEntry dirState(const QString &path)
{
    StateEntry state;
    state.path = path;
    state.type = EntryType::Directory;
    state.syncedAtMs = 1234;
    return state;
}

const SyncAction *findAction(const SyncPlan &plan, ActionKind kind, const QString &path)
{
    for (const SyncAction &action : plan.actions) {
        if (action.kind == kind && action.path == path)
            return &action;
    }
    return nullptr;
}

} // namespace

void TestSyncPlanner::newFiles()
{
    {
        QMap<QString, LocalEntry> local;
        local.insert(QStringLiteral("a.txt"), localFile(QStringLiteral("a.txt")));
        const SyncPlan plan = SyncPlanner::build(local, {}, {});
        QCOMPARE(plan.actions.size(), 1);
        QVERIFY(findAction(plan, ActionKind::Upload, QStringLiteral("a.txt")));
    }
    {
        QMap<QString, RemoteEntry> remote;
        remote.insert(QStringLiteral("b.txt"),
                      remoteFile(QStringLiteral("b.txt"), QStringLiteral("\"e\"")));
        const SyncPlan plan = SyncPlanner::build({}, remote, {});
        QCOMPARE(plan.actions.size(), 1);
        QVERIFY(findAction(plan, ActionKind::Download, QStringLiteral("b.txt")));
    }
}

void TestSyncPlanner::newDirectories()
{
    QMap<QString, LocalEntry> local;
    local.insert(QStringLiteral("docs"), localDir(QStringLiteral("docs")));
    local.insert(QStringLiteral("docs/a.txt"), localFile(QStringLiteral("docs/a.txt")));
    const SyncPlan plan = SyncPlanner::build(local, {}, {});
    QCOMPARE(plan.actions.size(), 2);
    QCOMPARE(plan.actions.at(0).kind, ActionKind::CreateRemoteDir);
    QCOMPARE(plan.actions.at(0).path, QStringLiteral("docs"));
    QCOMPARE(plan.actions.at(1).kind, ActionKind::Upload);

    QMap<QString, RemoteEntry> remote;
    remote.insert(QStringLiteral("empty"), remoteDir(QStringLiteral("empty")));
    const SyncPlan remotePlan = SyncPlanner::build({}, remote, {});
    QCOMPARE(remotePlan.actions.size(), 1);
    QCOMPARE(remotePlan.actions.at(0).kind, ActionKind::CreateLocalDir);
}

void TestSyncPlanner::bothNewDirectoriesRecordBaseline()
{
    QMap<QString, LocalEntry> local;
    local.insert(QStringLiteral("docs"), localDir(QStringLiteral("docs")));
    QMap<QString, RemoteEntry> remote;
    remote.insert(QStringLiteral("docs"), remoteDir(QStringLiteral("docs")));
    const SyncPlan plan = SyncPlanner::build(local, remote, {});
    QVERIFY(plan.actions.isEmpty());
    QCOMPARE(plan.baselineUpdates.size(), 1);
    QCOMPARE(plan.baselineUpdates.at(0).path, QStringLiteral("docs"));
    QCOMPARE(plan.baselineUpdates.at(0).type, EntryType::Directory);
}

void TestSyncPlanner::changedFiles()
{
    QMap<QString, LocalEntry> local;
    local.insert(QStringLiteral("a"), localFile(QStringLiteral("a"), 4, 2000));
    QMap<QString, RemoteEntry> remote;
    remote.insert(QStringLiteral("a"), remoteFile(QStringLiteral("a"), QStringLiteral("\"e1\"")));
    QHash<QString, StateEntry> state;
    state.insert(QStringLiteral("a"), fileState(QStringLiteral("a")));
    {
        const SyncPlan plan = SyncPlanner::build(local, remote, state);
        QCOMPARE(plan.actions.size(), 1);
        QVERIFY(findAction(plan, ActionKind::Upload, QStringLiteral("a")));
    }
    {
        remote[QStringLiteral("a")] = remoteFile(QStringLiteral("a"), QStringLiteral("\"e2\""));
        local[QStringLiteral("a")] = localFile(QStringLiteral("a"), 4, 1000);
        const SyncPlan plan = SyncPlanner::build(local, remote, state);
        QCOMPARE(plan.actions.size(), 1);
        QVERIFY(findAction(plan, ActionKind::Download, QStringLiteral("a")));
    }
    {
        local[QStringLiteral("a")] = localFile(QStringLiteral("a"), 4, 2000);
        const SyncPlan plan = SyncPlanner::build(local, remote, state);
        QCOMPARE(plan.actions.size(), 1);
        QCOMPARE(plan.actions.at(0).kind, ActionKind::ResolveConflict);
        QCOMPARE(plan.conflicts.size(), 1);
    }
}

void TestSyncPlanner::deletionPropagation()
{
    {
        QMap<QString, RemoteEntry> remote;
        remote.insert(QStringLiteral("a"),
                      remoteFile(QStringLiteral("a"), QStringLiteral("\"e1\"")));
        QHash<QString, StateEntry> state;
        state.insert(QStringLiteral("a"), fileState(QStringLiteral("a")));
        const SyncPlan plan = SyncPlanner::build({}, remote, state);
        QCOMPARE(plan.actions.size(), 1);
        QVERIFY(findAction(plan, ActionKind::DeleteRemote, QStringLiteral("a")));
    }
    {
        QMap<QString, LocalEntry> local;
        local.insert(QStringLiteral("a"), localFile(QStringLiteral("a")));
        QHash<QString, StateEntry> state;
        state.insert(QStringLiteral("a"), fileState(QStringLiteral("a")));
        const SyncPlan plan = SyncPlanner::build(local, {}, state);
        QCOMPARE(plan.actions.size(), 1);
        QVERIFY(findAction(plan, ActionKind::DeleteLocal, QStringLiteral("a")));
    }
}

void TestSyncPlanner::deletionConflicts()
{
    {
        QMap<QString, LocalEntry> local;
        local.insert(QStringLiteral("a"), localFile(QStringLiteral("a"), 4, 2000));
        QHash<QString, StateEntry> state;
        state.insert(QStringLiteral("a"), fileState(QStringLiteral("a")));
        const SyncPlan plan = SyncPlanner::build(local, {}, state);
        QCOMPARE(plan.actions.size(), 1);
        QCOMPARE(plan.actions.at(0).kind, ActionKind::ResolveConflict);
    }
    {
        QMap<QString, RemoteEntry> remote;
        remote.insert(QStringLiteral("a"),
                      remoteFile(QStringLiteral("a"), QStringLiteral("\"e2\"")));
        QHash<QString, StateEntry> state;
        state.insert(QStringLiteral("a"), fileState(QStringLiteral("a")));
        const SyncPlan plan = SyncPlanner::build({}, remote, state);
        QCOMPARE(plan.actions.size(), 1);
        QCOMPARE(plan.actions.at(0).kind, ActionKind::ResolveConflict);
    }
}

void TestSyncPlanner::stateOnlyEntriesAreRemoved()
{
    QHash<QString, StateEntry> state;
    state.insert(QStringLiteral("gone"), fileState(QStringLiteral("gone")));
    const SyncPlan plan = SyncPlanner::build({}, {}, state);
    QVERIFY(plan.actions.isEmpty());
    QCOMPARE(plan.removedStates, QStringList{QStringLiteral("gone")});
}

void TestSyncPlanner::directoryDeletionPropagation()
{
    {
        QMap<QString, RemoteEntry> remote;
        remote.insert(QStringLiteral("dir"), remoteDir(QStringLiteral("dir")));
        remote.insert(QStringLiteral("dir/a"),
                      remoteFile(QStringLiteral("dir/a"), QStringLiteral("\"e1\"")));
        QHash<QString, StateEntry> state;
        state.insert(QStringLiteral("dir"), dirState(QStringLiteral("dir")));
        state.insert(QStringLiteral("dir/a"), fileState(QStringLiteral("dir/a")));
        const SyncPlan plan = SyncPlanner::build({}, remote, state);
        QCOMPARE(plan.actions.size(), 1);
        QCOMPARE(plan.actions.at(0).kind, ActionKind::DeleteRemote);
        QCOMPARE(plan.actions.at(0).path, QStringLiteral("dir"));
    }
    {
        QMap<QString, LocalEntry> local;
        local.insert(QStringLiteral("dir"), localDir(QStringLiteral("dir")));
        local.insert(QStringLiteral("dir/a"), localFile(QStringLiteral("dir/a")));
        QHash<QString, StateEntry> state;
        state.insert(QStringLiteral("dir"), dirState(QStringLiteral("dir")));
        state.insert(QStringLiteral("dir/a"), fileState(QStringLiteral("dir/a")));
        const SyncPlan plan = SyncPlanner::build(local, {}, state);
        QCOMPARE(plan.actions.size(), 1);
        QCOMPARE(plan.actions.at(0).kind, ActionKind::DeleteLocal);
        QCOMPARE(plan.actions.at(0).path, QStringLiteral("dir"));
    }
}

void TestSyncPlanner::modifiedChildBlocksDirectoryDeletion()
{
    QMap<QString, LocalEntry> local;
    local.insert(QStringLiteral("dir"), localDir(QStringLiteral("dir")));
    local.insert(QStringLiteral("dir/a"), localFile(QStringLiteral("dir/a"), 4, 2000));
    QHash<QString, StateEntry> state;
    state.insert(QStringLiteral("dir"), dirState(QStringLiteral("dir")));
    state.insert(QStringLiteral("dir/a"), fileState(QStringLiteral("dir/a")));

    const SyncPlan plan = SyncPlanner::build(local, {}, state);
    QVERIFY(!findAction(plan, ActionKind::DeleteLocal, QStringLiteral("dir")));
    QVERIFY(findAction(plan, ActionKind::CreateRemoteDir, QStringLiteral("dir")));
    QVERIFY(findAction(plan, ActionKind::ResolveConflict, QStringLiteral("dir/a")));
}

void TestSyncPlanner::typeMismatchIsConflict()
{
    QMap<QString, LocalEntry> local;
    local.insert(QStringLiteral("thing"), localFile(QStringLiteral("thing")));
    QMap<QString, RemoteEntry> remote;
    remote.insert(QStringLiteral("thing"), remoteDir(QStringLiteral("thing")));
    const SyncPlan plan = SyncPlanner::build(local, remote, {});
    QCOMPARE(plan.actions.size(), 1);
    QCOMPARE(plan.actions.at(0).kind, ActionKind::ResolveConflict);
    QCOMPARE(plan.conflicts.size(), 1);
}

void TestSyncPlanner::nestedDirectoriesAreOrdered()
{
    QMap<QString, LocalEntry> local;
    local.insert(QStringLiteral("a"), localDir(QStringLiteral("a")));
    local.insert(QStringLiteral("a/b"), localDir(QStringLiteral("a/b")));
    local.insert(QStringLiteral("a/b/c"), localFile(QStringLiteral("a/b/c")));
    const SyncPlan plan = SyncPlanner::build(local, {}, {});
    QCOMPARE(plan.actions.size(), 3);
    QCOMPARE(plan.actions.at(0).path, QStringLiteral("a"));
    QCOMPARE(plan.actions.at(0).kind, ActionKind::CreateRemoteDir);
    QCOMPARE(plan.actions.at(1).path, QStringLiteral("a/b"));
    QCOMPARE(plan.actions.at(1).kind, ActionKind::CreateRemoteDir);
    QCOMPARE(plan.actions.at(2).kind, ActionKind::Upload);
}

void TestSyncPlanner::directoryStateResetWhenBothSidesBecameDirectories()
{
    QMap<QString, LocalEntry> local;
    local.insert(QStringLiteral("thing"), localDir(QStringLiteral("thing")));
    QMap<QString, RemoteEntry> remote;
    remote.insert(QStringLiteral("thing"), remoteDir(QStringLiteral("thing")));
    QHash<QString, StateEntry> state;
    state.insert(QStringLiteral("thing"), fileState(QStringLiteral("thing")));
    const SyncPlan plan = SyncPlanner::build(local, remote, state);
    QVERIFY(plan.actions.isEmpty());
    QCOMPARE(plan.baselineUpdates.size(), 1);
    QCOMPARE(plan.baselineUpdates.at(0).type, EntryType::Directory);
}

void TestSyncPlanner::localDeletionsAreRestoredWhenDisabled()
{
    {
        QMap<QString, RemoteEntry> remote;
        remote.insert(QStringLiteral("a.txt"),
                      remoteFile(QStringLiteral("a.txt"), QStringLiteral("\"e1\"")));
        QHash<QString, StateEntry> state;
        state.insert(QStringLiteral("a.txt"), fileState(QStringLiteral("a.txt")));
        const SyncPlan plan = SyncPlanner::build({}, remote, state, false);
        QCOMPARE(plan.actions.size(), 1);
        QCOMPARE(plan.actions.at(0).kind, ActionKind::Download);
        QVERIFY(!findAction(plan, ActionKind::DeleteRemote, QStringLiteral("a.txt")));
    }
    {
        QMap<QString, RemoteEntry> remote;
        remote.insert(QStringLiteral("dir"), remoteDir(QStringLiteral("dir")));
        remote.insert(QStringLiteral("dir/a"),
                      remoteFile(QStringLiteral("dir/a"), QStringLiteral("\"e1\"")));
        QHash<QString, StateEntry> state;
        state.insert(QStringLiteral("dir"), dirState(QStringLiteral("dir")));
        state.insert(QStringLiteral("dir/a"), fileState(QStringLiteral("dir/a")));
        const SyncPlan plan = SyncPlanner::build({}, remote, state, false);
        QVERIFY(findAction(plan, ActionKind::CreateLocalDir, QStringLiteral("dir")));
        QVERIFY(findAction(plan, ActionKind::Download, QStringLiteral("dir/a")));
        QVERIFY(!findAction(plan, ActionKind::DeleteRemote, QStringLiteral("dir")));
    }
}

void TestSyncPlanner::remoteDeletionsStillPropagateWhenLocalDeletionSyncIsDisabled()
{
    QMap<QString, LocalEntry> local;
    local.insert(QStringLiteral("a.txt"), localFile(QStringLiteral("a.txt")));
    QHash<QString, StateEntry> state;
    state.insert(QStringLiteral("a.txt"), fileState(QStringLiteral("a.txt")));
    const SyncPlan plan = SyncPlanner::build(local, {}, state, false);
    QCOMPARE(plan.actions.size(), 1);
    QCOMPARE(plan.actions.at(0).kind, ActionKind::DeleteLocal);
}

} // namespace NutSync::Test
