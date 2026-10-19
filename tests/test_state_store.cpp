#include "test_state_store.h"

#include "core/sync_state_store.h"

#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

namespace NutSync::Test {

void TestStateStore::persistsConflicts()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("state.db"));

    SyncStateStore store(path);
    QString error;
    QVERIFY2(store.open(&error), qPrintable(error));
    const QList<ConflictRecord> records = {
        {QStringLiteral("doc.txt"), QStringLiteral("doc.conflict-1.txt")},
        {QStringLiteral("other.txt"), QString()},
    };
    QVERIFY2(store.replaceConflicts(QStringLiteral("pair"), records, &error), qPrintable(error));

    QList<ConflictRecord> loaded;
    QVERIFY2(store.loadConflicts(QStringLiteral("pair"), &loaded, &error), qPrintable(error));
    QCOMPARE(loaded.size(), 2);
    QCOMPARE(loaded.at(0).path, QStringLiteral("doc.txt"));
    QCOMPARE(loaded.at(0).copyPath, QStringLiteral("doc.conflict-1.txt"));
    QCOMPARE(loaded.at(1).path, QStringLiteral("other.txt"));
    QVERIFY(loaded.at(1).copyPath.isEmpty());
}

void TestStateStore::migratesVersionOne()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("state.db"));
    const QString connection = QStringLiteral("v1-migration-test");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(path);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version=1")));
        database.close();
    }
    QSqlDatabase::removeDatabase(connection);

    SyncStateStore store(path);
    QString error;
    QVERIFY2(store.open(&error), qPrintable(error));
    QList<ConflictRecord> records;
    QVERIFY2(store.loadConflicts(QStringLiteral("pair"), &records, &error), qPrintable(error));

    const QStringList backups =
        QDir(directory.path())
            .entryList({QStringLiteral("state.db.bak-pre-migration-v1-*")}, QDir::Files);
    QCOMPARE(backups.size(), 1);
}

void TestStateStore::rejectsFutureSchemaVersion()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("state.db"));

    {
        SyncStateStore store(path);
        QString error;
        QVERIFY2(store.open(&error), qPrintable(error));
    }

    const QString connection = QStringLiteral("future-version-test");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(path);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version=99")));
        database.close();
    }
    QSqlDatabase::removeDatabase(connection);

    SyncStateStore store(path);
    QString error;
    QVERIFY(!store.open(&error));
    QVERIFY2(error.contains(QStringLiteral("99")), qPrintable(error));
}

void TestStateStore::writesAndLoadsHistory()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SyncStateStore store(directory.filePath(QStringLiteral("state.db")));
    QString error;
    QVERIFY2(store.open(&error), qPrintable(error));

    SyncHistoryEntry first;
    first.finishedAtMs = 1000;
    first.success = true;
    first.uploaded = 2;
    first.downloaded = 1;
    first.conflicts = 1;
    first.elapsedMs = 1234;
    first.message = QStringLiteral("first");
    QVERIFY2(store.appendHistory(QStringLiteral("pair"), first, &error), qPrintable(error));

    SyncHistoryEntry second;
    second.finishedAtMs = 2000;
    second.success = false;
    second.errors = 1;
    second.message = QStringLiteral("second");
    QVERIFY2(store.appendHistory(QStringLiteral("pair"), second, &error), qPrintable(error));

    QList<SyncHistoryEntry> history;
    QVERIFY2(store.loadHistory(QStringLiteral("pair"), 10, &history, &error), qPrintable(error));
    QCOMPARE(history.size(), 2);
    QCOMPARE(history.at(0).message, QStringLiteral("second"));
    QCOMPARE(history.at(1).uploaded, 2);
    QCOMPARE(history.at(1).elapsedMs, 1234);

    QVERIFY2(store.clearHistory(QStringLiteral("pair"), &error), qPrintable(error));
    QVERIFY2(store.loadHistory(QStringLiteral("pair"), 10, &history, &error), qPrintable(error));
    QVERIFY(history.isEmpty());
}

} // namespace NutSync::Test
