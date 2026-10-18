#include "test_trash_manager.h"

#include "core/trash_manager.h"

#include <QDir>
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

} // namespace NutSync::Test
