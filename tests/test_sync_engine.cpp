#include "test_sync_engine.h"

#include "core/trash_manager.h"

#include "core/sync_engine.h"
#include "core/sync_state_store.h"
#include "mock_remote_backend.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTest>
#include <QTemporaryDir>

#include <memory>

namespace NutSync::Test {

namespace {

class Harness
{
public:
    Harness()
        : m_local(m_dir.filePath(QStringLiteral("local")))
        , m_remote(m_dir.filePath(QStringLiteral("remote")))
        , m_database(m_dir.filePath(QStringLiteral("state.db")))
    {
        QDir().mkpath(m_local);
        QDir().mkpath(m_remote);

        m_pair.id = QStringLiteral("test-pair");
        m_pair.name = QStringLiteral("Test Pair");
        m_pair.localPath = m_local;
        m_pair.remoteUrl = QStringLiteral("mock://remote/");
        m_pair.enabled = true;
        m_pair.intervalMinutes = 0;
    }

    QString localPath(const QString &relative) const { return QDir(m_local).filePath(relative); }

    QString remotePath(const QString &relative) const { return QDir(m_remote).filePath(relative); }

    QString pairId() const { return m_pair.id; }
    void setIncludePaths(const QStringList &paths) { m_pair.includePaths = paths; }

    bool writeLocal(const QString &relative, const QByteArray &content)
    {
        return writeFile(localPath(relative), content);
    }

    bool writeRemote(const QString &relative, const QByteArray &content)
    {
        return writeFile(remotePath(relative), content);
    }

    bool writeFile(const QString &path, const QByteArray &content)
    {
        if (!QDir().mkpath(QFileInfo(path).absolutePath()))
            return false;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        if (file.write(content) != content.size())
            return false;
        file.close();
        if (file.open(QIODevice::ReadWrite))
            file.setFileTime(QDateTime::currentDateTime().addMSecs(++m_clock),
                             QFileDevice::FileModificationTime);
        return true;
    }

    QByteArray readLocal(const QString &relative, bool *ok = nullptr) const
    {
        return readFile(localPath(relative), ok);
    }

    QByteArray readRemote(const QString &relative, bool *ok = nullptr) const
    {
        return readFile(remotePath(relative), ok);
    }

    static QByteArray readFile(const QString &path, bool *ok = nullptr)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            if (ok)
                *ok = false;
            return {};
        }
        if (ok)
            *ok = true;
        return file.readAll();
    }

    bool existsLocal(const QString &relative) const
    {
        return QFileInfo::exists(localPath(relative));
    }

    bool existsRemote(const QString &relative) const
    {
        return QFileInfo::exists(remotePath(relative));
    }

    void removeLocal(const QString &relative)
    {
        const QString path = localPath(relative);
        if (QFileInfo(path).isDir())
            QDir(path).removeRecursively();
        else
            QFile::remove(path);
    }

    void removeRemote(const QString &relative)
    {
        const QString path = remotePath(relative);
        if (QFileInfo(path).isDir())
            QDir(path).removeRecursively();
        else
            QFile::remove(path);
    }

    QStringList listLocal(const QString &relative, const QString &filter = QString()) const
    {
        return QDir(localPath(relative))
            .entryList(filter.isEmpty() ? QStringList{QStringLiteral("*")} : QStringList{filter},
                       QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    }

    SyncSummary sync(ConflictPolicy policy = ConflictPolicy::KeepBoth,
                     const QStringList &excludes = {}, bool syncDeletions = true)
    {
        m_pair.conflictPolicy = policy;
        m_pair.excludePatterns = excludes;
        m_pair.syncDeletions = syncDeletions;
        SyncEngine engine(m_pair, m_database);
        engine.setBackendFactory([remote = m_remote]() {
            return std::make_unique<MockRemoteBackend>(remote);
        });
        SyncSummary result;
        QObject::connect(&engine, &SyncEngine::finished, &engine,
                         [&result](const SyncSummary &summary) {
                             result = summary;
                         });
        QObject::connect(&engine, &SyncEngine::logMessage, &engine, [this](const QString &message) {
            m_logs.append(message);
        });
        engine.run();
        return result;
    }

    QString diagnostics(const SyncSummary &summary) const
    {
        return summary.message + QStringLiteral("\n") + summary.errorDetails.join('\n')
            + QStringLiteral("\nlogs:\n") + m_logs.join('\n');
    }

    QString remoteRoot() const { return m_remote; }

private:
    QTemporaryDir m_dir;
    QString m_local;
    QString m_remote;
    QString m_database;
    SyncPair m_pair;
    QStringList m_logs;
    qint64 m_clock = 0;
};

} // namespace

void TestSyncEngine::basicBidirectionalTransfer()
{
    Harness harness;
    QVERIFY(harness.writeLocal(QStringLiteral("local.txt"), "LOCAL"));
    QVERIFY(harness.writeRemote(QStringLiteral("remote.txt"), "REMOTE"));

    const SyncSummary first = harness.sync();
    QVERIFY2(first.success, qPrintable(harness.diagnostics(first)));
    QCOMPARE(first.uploaded, 1);
    QCOMPARE(first.downloaded, 1);
    QCOMPARE(harness.readRemote(QStringLiteral("local.txt")), QByteArray("LOCAL"));
    QCOMPARE(harness.readLocal(QStringLiteral("remote.txt")), QByteArray("REMOTE"));

    const SyncSummary second = harness.sync();
    QVERIFY2(second.success, qPrintable(harness.diagnostics(second)));
    QCOMPARE(second.uploaded, 0);
    QCOMPARE(second.downloaded, 0);
    QCOMPARE(second.errors, 0);
}

void TestSyncEngine::detectsSubsequentChanges()
{
    Harness harness;
    QVERIFY(harness.writeLocal(QStringLiteral("a.txt"), "one"));
    SyncSummary summary = harness.sync();
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));

    QVERIFY(harness.writeRemote(QStringLiteral("a.txt"), "remote-two"));
    summary = harness.sync();
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QCOMPARE(summary.downloaded, 1);
    QCOMPARE(harness.readLocal(QStringLiteral("a.txt")), QByteArray("remote-two"));

    QVERIFY(harness.writeLocal(QStringLiteral("a.txt"), "local-three"));
    summary = harness.sync();
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QCOMPARE(summary.uploaded, 1);
    QCOMPARE(harness.readRemote(QStringLiteral("a.txt")), QByteArray("local-three"));
}

void TestSyncEngine::sameContentMtimeChangeDoesNotUpload()
{
    Harness harness;
    QVERIFY(harness.writeLocal(QStringLiteral("touched.txt"), "same-content"));
    SyncSummary summary = harness.sync();
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QCOMPARE(summary.uploaded, 1);

    // Rewriting identical bytes changes the local mtime but not the content.
    QVERIFY(harness.writeLocal(QStringLiteral("touched.txt"), "same-content"));
    summary = harness.sync();
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QCOMPARE(summary.uploaded, 0);
    QCOMPARE(summary.skipped, 1);
    QCOMPARE(harness.readRemote(QStringLiteral("touched.txt")), QByteArray("same-content"));

    // A same-size content change must still be uploaded.
    QVERIFY(harness.writeLocal(QStringLiteral("touched.txt"), "new-content!"));
    summary = harness.sync();
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QCOMPARE(summary.uploaded, 1);
    QCOMPARE(harness.readRemote(QStringLiteral("touched.txt")), QByteArray("new-content!"));
}

void TestSyncEngine::selectiveSync()
{
    Harness harness;
    harness.setIncludePaths({QStringLiteral("docs")});
    QVERIFY(harness.writeLocal(QStringLiteral("docs/inside.txt"), "inside"));
    QVERIFY(harness.writeLocal(QStringLiteral("outside.txt"), "outside"));

    const SyncSummary summary = harness.sync(ConflictPolicy::KeepBoth, {}, true);
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QCOMPARE(harness.readRemote(QStringLiteral("docs/inside.txt")), QByteArray("inside"));
    QVERIFY(!harness.existsRemote(QStringLiteral("outside.txt")));
    QVERIFY(harness.existsLocal(QStringLiteral("outside.txt")));
}

void TestSyncEngine::deletedFilesAreBackedUp()
{
    Harness harness;
    QVERIFY(harness.writeLocal(QStringLiteral("backup.txt"), "keep me"));
    const SyncSummary first = harness.sync(ConflictPolicy::KeepBoth, {}, true);
    QVERIFY2(first.success, qPrintable(harness.diagnostics(first)));

    QVERIFY(QFile::remove(harness.localPath(QStringLiteral("backup.txt"))));
    const SyncSummary second = harness.sync(ConflictPolicy::KeepBoth, {}, true);
    QVERIFY2(second.success, qPrintable(harness.diagnostics(second)));
    QCOMPARE(second.deletedRemote, 1);

    const QString trashRoot = TrashManager::pairDir(harness.pairId());
    bool found = false;
    QDirIterator iterator(trashRoot, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        if (path.endsWith(QStringLiteral("/backup.txt"))) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), QByteArray("keep me"));
            found = true;
        }
    }
    QVERIFY(found);
}

void TestSyncEngine::propagatesRemoteDeletion()
{
    Harness harness;
    QVERIFY(harness.writeLocal(QStringLiteral("a.txt"), "value"));
    SyncSummary summary = harness.sync();
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));

    harness.removeLocal(QStringLiteral("a.txt"));
    summary = harness.sync();
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QCOMPARE(summary.deletedRemote, 1);
    QVERIFY(!harness.existsRemote(QStringLiteral("a.txt")));
}

void TestSyncEngine::propagatesLocalDeletion()
{
    Harness harness;
    QVERIFY(harness.writeRemote(QStringLiteral("b.txt"), "value"));
    SyncSummary summary = harness.sync();
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));

    harness.removeRemote(QStringLiteral("b.txt"));
    summary = harness.sync();
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QCOMPARE(summary.deletedLocal, 1);
    QVERIFY(!harness.existsLocal(QStringLiteral("b.txt")));
}

void TestSyncEngine::handlesEmptyDirectories()
{
    Harness harness;
    QVERIFY(QDir().mkpath(harness.localPath(QStringLiteral("empty-local"))));
    QVERIFY(QDir().mkpath(harness.remotePath(QStringLiteral("empty-remote"))));

    const SyncSummary summary = harness.sync();
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QCOMPARE(summary.directoriesCreated, 2);
    QVERIFY(QFileInfo(harness.remotePath(QStringLiteral("empty-local"))).isDir());
    QVERIFY(QFileInfo(harness.localPath(QStringLiteral("empty-remote"))).isDir());
}

void TestSyncEngine::propagatesDirectoryDeletion()
{
    Harness harness;
    QVERIFY(harness.writeLocal(QStringLiteral("dir/a.txt"), "A"));
    QVERIFY(harness.writeLocal(QStringLiteral("dir/sub/b.txt"), "B"));
    SyncSummary summary = harness.sync();
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QVERIFY(harness.existsRemote(QStringLiteral("dir/sub/b.txt")));

    harness.removeLocal(QStringLiteral("dir"));
    summary = harness.sync();
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QVERIFY(!harness.existsRemote(QStringLiteral("dir")));

    QVERIFY(harness.writeRemote(QStringLiteral("rdir/x.txt"), "X"));
    summary = harness.sync();
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QVERIFY(harness.existsLocal(QStringLiteral("rdir/x.txt")));

    harness.removeRemote(QStringLiteral("rdir"));
    summary = harness.sync();
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QVERIFY(!harness.existsLocal(QStringLiteral("rdir")));
}

void TestSyncEngine::keepsBothConflictCopies()
{
    Harness harness;
    QVERIFY(harness.writeLocal(QStringLiteral("doc.txt"), "local version"));
    QVERIFY(harness.writeRemote(QStringLiteral("doc.txt"), "remote version"));

    const SyncSummary summary = harness.sync(ConflictPolicy::KeepBoth);
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QCOMPARE(summary.conflicts, 1);
    QCOMPARE(harness.readLocal(QStringLiteral("doc.txt")), QByteArray("local version"));
    QCOMPARE(harness.readRemote(QStringLiteral("doc.txt")), QByteArray("local version"));

    const QStringList localConflict =
        harness.listLocal(QString(), QStringLiteral("doc.conflict-*.txt"));
    QCOMPARE(localConflict.size(), 1);
    const QString conflictName = localConflict.constFirst();
    QCOMPARE(harness.readLocal(conflictName), QByteArray("remote version"));
    QCOMPARE(harness.readRemote(conflictName), QByteArray("remote version"));
    QCOMPARE(summary.conflictCopies, QStringList{conflictName});

    const SyncSummary second = harness.sync(ConflictPolicy::KeepBoth);
    QVERIFY2(second.success, qPrintable(harness.diagnostics(second)));
    QCOMPARE(second.conflicts, 0);
    QCOMPARE(second.uploaded, 0);
    QCOMPARE(second.downloaded, 0);
}

void TestSyncEngine::preferLocalConflict()
{
    Harness harness;
    QVERIFY(harness.writeLocal(QStringLiteral("doc.txt"), "local wins"));
    QVERIFY(harness.writeRemote(QStringLiteral("doc.txt"), "remote loses"));
    const SyncSummary summary = harness.sync(ConflictPolicy::PreferLocal);
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QCOMPARE(summary.conflicts, 1);
    QCOMPARE(harness.readRemote(QStringLiteral("doc.txt")), QByteArray("local wins"));
}

void TestSyncEngine::preferRemoteConflict()
{
    Harness harness;
    QVERIFY(harness.writeLocal(QStringLiteral("doc.txt"), "local loses"));
    QVERIFY(harness.writeRemote(QStringLiteral("doc.txt"), "remote wins"));
    const SyncSummary summary = harness.sync(ConflictPolicy::PreferRemote);
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QCOMPARE(summary.conflicts, 1);
    QCOMPARE(harness.readLocal(QStringLiteral("doc.txt")), QByteArray("remote wins"));
}

void TestSyncEngine::identicalInitialFilesDoNotConflict()
{
    Harness harness;
    QVERIFY(harness.writeLocal(QStringLiteral("same.txt"), "identical"));
    QVERIFY(harness.writeRemote(QStringLiteral("same.txt"), "identical"));
    const SyncSummary summary = harness.sync(ConflictPolicy::KeepBoth);
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QCOMPARE(summary.conflicts, 0);
    QVERIFY(harness.listLocal(QString(), QStringLiteral("same.conflict-*")).isEmpty());
}

void TestSyncEngine::excludedLocalEntriesAreNotUploaded()
{
    Harness harness;
    QVERIFY(harness.writeLocal(QStringLiteral(".git/config"), "secret"));
    QVERIFY(harness.writeLocal(QStringLiteral("keep.txt"), "keep"));
    const SyncSummary summary = harness.sync(ConflictPolicy::KeepBoth, {QStringLiteral(".git")});
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QVERIFY(!harness.existsRemote(QStringLiteral(".git/config")));
    QVERIFY(harness.existsRemote(QStringLiteral("keep.txt")));
}

void TestSyncEngine::excludedRemoteContentBlocksDirectoryDeletion()
{
    Harness harness;
    QVERIFY(harness.writeLocal(QStringLiteral("dir/file.txt"), "file"));
    SyncSummary summary = harness.sync();
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QVERIFY(harness.writeRemote(QStringLiteral("dir/secret.tmp"), "do not delete"));

    harness.removeLocal(QStringLiteral("dir"));
    summary = harness.sync(ConflictPolicy::KeepBoth, {QStringLiteral("*.tmp")});
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QVERIFY(summary.skipped >= 1);
    QVERIFY(!harness.existsLocal(QStringLiteral("dir")));
    QVERIFY(harness.existsRemote(QStringLiteral("dir/secret.tmp")));
    QVERIFY(harness.existsRemote(QStringLiteral("dir/file.txt")));
}

void TestSyncEngine::typeConflictReportsError()
{
    Harness harness;
    QVERIFY(harness.writeLocal(QStringLiteral("thing"), "local file"));
    QVERIFY(QDir().mkpath(harness.remotePath(QStringLiteral("thing"))));
    const SyncSummary summary = harness.sync();
    QVERIFY(!summary.success);
    QVERIFY(summary.errors >= 1);
    QVERIFY(harness.existsLocal(QStringLiteral("thing")));
    QVERIFY(QFileInfo(harness.remotePath(QStringLiteral("thing"))).isDir());
}

void TestSyncEngine::missingRemoteRootWithStateDoesNotDeleteLocalFiles()
{
    Harness harness;
    QVERIFY(harness.writeLocal(QStringLiteral("keep.txt"), "important"));
    SyncSummary summary = harness.sync();
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QVERIFY(harness.existsRemote(QStringLiteral("keep.txt")));

    QVERIFY(QDir(harness.remoteRoot()).removeRecursively());
    summary = harness.sync();
    QVERIFY(!summary.success);
    QVERIFY(harness.existsLocal(QStringLiteral("keep.txt")));
    QCOMPARE(harness.readLocal(QStringLiteral("keep.txt")), QByteArray("important"));
    QVERIFY(!QFileInfo::exists(harness.remoteRoot()));
}

void TestSyncEngine::createsRemoteRootForFirstSync()
{
    Harness harness;
    QVERIFY(harness.writeLocal(QStringLiteral("first.txt"), "first"));
    QVERIFY(QDir(harness.remoteRoot()).removeRecursively());
    const SyncSummary summary = harness.sync();
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QVERIFY(QFileInfo(harness.remoteRoot()).isDir());
    QVERIFY(harness.existsRemote(QStringLiteral("first.txt")));
}

void TestSyncEngine::cancelledBeforeStartReportsCancellation()
{
    QTemporaryDir localDir;
    QTemporaryDir remoteDir;
    QTemporaryDir stateDir;
    QVERIFY(localDir.isValid() && remoteDir.isValid() && stateDir.isValid());

    SyncPair pair;
    pair.id = QStringLiteral("cancel-test");
    pair.name = QStringLiteral("Cancel Test");
    pair.localPath = localDir.path();
    pair.remoteUrl = QStringLiteral("mock://remote/");
    pair.enabled = true;

    SyncEngine engine(pair, stateDir.filePath(QStringLiteral("state.db")));
    engine.setBackendFactory([remote = remoteDir.path()]() {
        return std::make_unique<MockRemoteBackend>(remote);
    });
    SyncSummary result;
    QObject::connect(&engine, &SyncEngine::finished, &engine,
                     [&result](const SyncSummary &summary) {
                         result = summary;
                     });
    engine.requestCancel();
    engine.run();
    QVERIFY(result.cancelled);
    QVERIFY(!result.success);
    QCOMPARE(result.message, QStringLiteral("同步已取消"));
}

void TestSyncEngine::localSymlinkDoesNotDeleteRemoteFile()
{
    Harness harness;
    QVERIFY(harness.writeLocal(QStringLiteral("replace-me.txt"), "remote copy"));
    SyncSummary summary = harness.sync();
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QVERIFY(harness.existsRemote(QStringLiteral("replace-me.txt")));

    harness.removeLocal(QStringLiteral("replace-me.txt"));
    const QString linkTarget = harness.localPath(QStringLiteral("target.txt"));
    QVERIFY(harness.writeLocal(QStringLiteral("target.txt"), "target"));
#ifdef Q_OS_UNIX
    QVERIFY(QFile::link(linkTarget, harness.localPath(QStringLiteral("replace-me.txt"))));
#else
    QSKIP("symlink test is Unix-only");
#endif

    summary = harness.sync(ConflictPolicy::KeepBoth);
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QVERIFY(harness.existsRemote(QStringLiteral("replace-me.txt")));
    QCOMPARE(harness.readRemote(QStringLiteral("replace-me.txt")), QByteArray("remote copy"));
    QCOMPARE(harness.readLocal(QStringLiteral("replace-me.txt")), QByteArray("target"));
}

void TestSyncEngine::localDeletionRestoresWhenSyncDeletionsDisabled()
{
    Harness harness;
    QVERIFY(harness.writeLocal(QStringLiteral("restore-me.txt"), "remote copy"));
    SyncSummary summary = harness.sync(ConflictPolicy::KeepBoth, {}, true);
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QVERIFY(harness.existsRemote(QStringLiteral("restore-me.txt")));

    harness.removeLocal(QStringLiteral("restore-me.txt"));
    summary = harness.sync(ConflictPolicy::KeepBoth, {}, false);
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QCOMPARE(summary.deletedRemote, 0);
    QCOMPARE(summary.downloaded, 1);
    QVERIFY(harness.existsRemote(QStringLiteral("restore-me.txt")));
    QCOMPARE(harness.readLocal(QStringLiteral("restore-me.txt")), QByteArray("remote copy"));
}

void TestSyncEngine::localDirectoryDeletionRestoresWhenSyncDeletionsDisabled()
{
    Harness harness;
    QVERIFY(harness.writeLocal(QStringLiteral("keepdir/sub/file.txt"), "content"));
    SyncSummary summary = harness.sync(ConflictPolicy::KeepBoth, {}, true);
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));

    harness.removeLocal(QStringLiteral("keepdir"));
    summary = harness.sync(ConflictPolicy::KeepBoth, {}, false);
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QVERIFY(harness.existsRemote(QStringLiteral("keepdir/sub/file.txt")));
    QCOMPARE(harness.readLocal(QStringLiteral("keepdir/sub/file.txt")), QByteArray("content"));
}

void TestSyncEngine::deletionConflictDoesNotDeleteRemoteWhenSyncDeletionsDisabled()
{
    Harness harness;
    QVERIFY(harness.writeLocal(QStringLiteral("conflict-delete.txt"), "initial"));
    SyncSummary summary = harness.sync(ConflictPolicy::KeepBoth, {}, true);
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));

    QVERIFY(harness.writeRemote(QStringLiteral("conflict-delete.txt"), "remote changed"));
    harness.removeLocal(QStringLiteral("conflict-delete.txt"));
    summary = harness.sync(ConflictPolicy::PreferLocal, {}, false);
    QVERIFY2(summary.success, qPrintable(harness.diagnostics(summary)));
    QVERIFY(harness.existsRemote(QStringLiteral("conflict-delete.txt")));
    QCOMPARE(harness.readRemote(QStringLiteral("conflict-delete.txt")),
             QByteArray("remote changed"));
    QCOMPARE(harness.readLocal(QStringLiteral("conflict-delete.txt")),
             QByteArray("remote changed"));
}

void TestSyncEngine::stateStoreSupportsPrefixRemoval()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SyncStateStore store(dir.filePath(QStringLiteral("state.db")));
    QString error;
    QVERIFY2(store.open(&error), qPrintable(error));

    StateEntry parent;
    parent.path = QStringLiteral("dir");
    parent.type = EntryType::Directory;
    StateEntry child;
    child.path = QStringLiteral("dir/file");
    child.type = EntryType::File;
    child.localSize = 4;
    child.localMtimeMs = 10;
    StateEntry other;
    other.path = QStringLiteral("dir2/file");
    other.type = EntryType::File;
    QVERIFY2(store.upsert(QStringLiteral("pair"), parent, &error), qPrintable(error));
    QVERIFY2(store.upsert(QStringLiteral("pair"), child, &error), qPrintable(error));
    QVERIFY2(store.upsert(QStringLiteral("pair"), other, &error), qPrintable(error));

    QHash<QString, StateEntry> loaded;
    QVERIFY2(store.load(QStringLiteral("pair"), &loaded, &error), qPrintable(error));
    QCOMPARE(loaded.size(), 3);

    QVERIFY2(store.removePrefix(QStringLiteral("pair"), QStringLiteral("dir"), &error),
             qPrintable(error));
    QVERIFY2(store.load(QStringLiteral("pair"), &loaded, &error), qPrintable(error));
    QCOMPARE(loaded.size(), 1);
    QVERIFY(loaded.contains(QStringLiteral("dir2/file")));

    QVERIFY2(store.clearPair(QStringLiteral("pair"), &error), qPrintable(error));
    QVERIFY2(store.load(QStringLiteral("pair"), &loaded, &error), qPrintable(error));
    QVERIFY(loaded.isEmpty());
}

} // namespace NutSync::Test
