#include "test_conflict_resolver.h"

#include "core/conflict_resolver.h"
#include "core/sync_state_store.h"

#include "mock_remote_backend.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace NutSync::Test {

namespace {
QString writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    file.write(content);
    file.close();
    return path;
}

SyncPair makePair(const QString &local, const QString &remote)
{
    Q_UNUSED(remote)
    SyncPair pair;
    pair.id = QStringLiteral("conflict-pair");
    pair.name = QStringLiteral("Conflict");
    pair.localPath = local;
    pair.remoteUrl = QStringLiteral("mock://remote/");
    return pair;
}
} // namespace

void TestConflictResolver::keepsRemoteVersion()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString local = directory.filePath(QStringLiteral("local"));
    const QString remote = directory.filePath(QStringLiteral("remote"));
    QVERIFY(QDir().mkpath(local));
    QVERIFY(QDir().mkpath(remote));

    QVERIFY(!writeFile(QDir(local).filePath(QStringLiteral("doc.txt")), QByteArray("local version"))
                 .isEmpty());
    QVERIFY(!writeFile(QDir(local).filePath(QStringLiteral("doc.conflict.txt")),
                       QByteArray("remote version"))
                 .isEmpty());
    MockRemoteBackend backend(remote);
    QString error;
    QVERIFY(backend.writeFile(QStringLiteral("doc.txt"), "local version", &error));
    QVERIFY(backend.writeFile(QStringLiteral("doc.conflict.txt"), "remote version", &error));

    const SyncPair pair = makePair(local, remote);
    const ConflictRecord record{QStringLiteral("doc.txt"), QStringLiteral("doc.conflict.txt")};
    const ConflictResolutionResult result =
        resolveConflict(pair, record, ConflictResolution::KeepRemote, backend,
                        directory.filePath(QStringLiteral("state.db")));
    QVERIFY2(result.success, qPrintable(result.error));

    QFile localMain(QDir(local).filePath(QStringLiteral("doc.txt")));
    QVERIFY(localMain.open(QIODevice::ReadOnly));
    QCOMPARE(localMain.readAll(), QByteArray("remote version"));
    QCOMPARE(backend.readFile(QStringLiteral("doc.txt")), QByteArray("remote version"));
    QVERIFY(!QFileInfo::exists(QDir(local).filePath(QStringLiteral("doc.conflict.txt"))));
    QVERIFY(!QFileInfo::exists(QDir(remote).filePath(QStringLiteral("doc.conflict.txt"))));

    SyncStateStore store(directory.filePath(QStringLiteral("state.db")));
    QVERIFY(store.open(&error));
    QHash<QString, StateEntry> state;
    QVERIFY(store.load(pair.id, &state, &error));
    QVERIFY(state.contains(QStringLiteral("doc.txt")));
    QVERIFY(!state.contains(QStringLiteral("doc.conflict.txt")));
}

void TestConflictResolver::keepsLocalVersion()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString local = directory.filePath(QStringLiteral("local"));
    const QString remote = directory.filePath(QStringLiteral("remote"));
    QVERIFY(QDir().mkpath(local));
    QVERIFY(QDir().mkpath(remote));
    QVERIFY(!writeFile(QDir(local).filePath(QStringLiteral("doc.txt")), QByteArray("local version"))
                 .isEmpty());
    QVERIFY(!writeFile(QDir(local).filePath(QStringLiteral("doc.conflict.txt")),
                       QByteArray("remote version"))
                 .isEmpty());
    MockRemoteBackend backend(remote);
    QString error;
    QVERIFY(backend.writeFile(QStringLiteral("doc.txt"), "local version", &error));
    QVERIFY(backend.writeFile(QStringLiteral("doc.conflict.txt"), "remote version", &error));

    const SyncPair pair = makePair(local, remote);
    const ConflictRecord record{QStringLiteral("doc.txt"), QStringLiteral("doc.conflict.txt")};
    const ConflictResolutionResult result =
        resolveConflict(pair, record, ConflictResolution::KeepLocal, backend,
                        directory.filePath(QStringLiteral("state.db")));
    QVERIFY2(result.success, qPrintable(result.error));

    QFile localMain(QDir(local).filePath(QStringLiteral("doc.txt")));
    QVERIFY(localMain.open(QIODevice::ReadOnly));
    QCOMPARE(localMain.readAll(), QByteArray("local version"));
    QCOMPARE(backend.readFile(QStringLiteral("doc.txt")), QByteArray("local version"));
    QVERIFY(!QFileInfo::exists(QDir(local).filePath(QStringLiteral("doc.conflict.txt"))));
    QVERIFY(!QFileInfo::exists(QDir(remote).filePath(QStringLiteral("doc.conflict.txt"))));
}

} // namespace NutSync::Test
