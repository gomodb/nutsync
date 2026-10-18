#include "test_state_store.h"

#include "core/sync_state_store.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

namespace NutSync::Test {

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

} // namespace NutSync::Test
