#include "test_trash_manager.h"

#include "core/trash_manager.h"

#include <QDir>
#include <QUuid>
#include <algorithm>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace NutSync::Test {

void TestTrashManager::copiesFilesAndDirectories()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourceRoot = directory.filePath(QStringLiteral("source"));
    QVERIFY(QDir().mkpath(sourceRoot + QStringLiteral("/nested")));
    QFile file(sourceRoot + QStringLiteral("/nested/file.txt"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("trash me");
    file.close();

    const QString batch = directory.filePath(QStringLiteral("batch"));
    QVERIFY(QDir().mkpath(batch));
    QString error;
    QVERIFY2(TrashManager::copyLocalPath(sourceRoot, batch, QStringLiteral("nested"), &error),
             qPrintable(error));
    QFile copied(batch + QStringLiteral("/nested/nested/file.txt"));
    QVERIFY(copied.open(QIODevice::ReadOnly));
    QCOMPARE(copied.readAll(), QByteArray("trash me"));
}

void TestTrashManager::listsAndRestoresEntries()
{
    const QString pairId =
        QStringLiteral("unit-pair-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString batch = TrashManager::createBatch(pairId, 0);
    QVERIFY(!batch.isEmpty());
    const QString source = batch + QStringLiteral("/docs/file.txt");
    QVERIFY(QDir().mkpath(QFileInfo(source).absolutePath()));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("restore me");
    file.close();

    const QList<TrashManager::TrashEntry> entries = TrashManager::listEntries(pairId);
    QCOMPARE(entries.size(), 2);
    const auto fileEntry =
        std::find_if(entries.cbegin(), entries.cend(), [](const TrashManager::TrashEntry &entry) {
            return entry.relativePath == QStringLiteral("docs/file.txt");
        });
    QVERIFY(fileEntry != entries.cend());
    QVERIFY(!fileEntry->directory);

    QTemporaryDir local;
    QVERIFY(local.isValid());
    QString error;
    QVERIFY2(TrashManager::restoreEntry(*fileEntry, local.path(), false, &error),
             qPrintable(error));
    QFile restored(local.filePath(QStringLiteral("docs/file.txt")));
    QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.readAll(), QByteArray("restore me"));

    QDir(TrashManager::pairDir(pairId)).removeRecursively();
}

} // namespace NutSync::Test
