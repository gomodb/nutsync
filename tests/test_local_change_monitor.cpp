#include "test_local_change_monitor.h"

#include "core/local_change_monitor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>

namespace NutSync::Test {

namespace {
bool writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(content) == content.size();
}
} // namespace

void TestLocalChangeMonitor::detectsFileChanges()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    LocalChangeMonitor monitor(dir.path(), {});
    QSignalSpy changed(&monitor, &LocalChangeMonitor::localChanged);
    QString error;
    QVERIFY2(monitor.start(&error), qPrintable(error));
    QVERIFY(monitor.isActive());
    QVERIFY(monitor.watchCount() >= 1);

    QVERIFY(writeFile(dir.filePath(QStringLiteral("hello.txt")), "hello"));
    QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 1, 5000);

    monitor.stop();
    QVERIFY(!monitor.isActive());
    changed.clear();
    QVERIFY(writeFile(dir.filePath(QStringLiteral("after-stop.txt")), "ignored"));
    QTest::qWait(1800);
    QCOMPARE(changed.count(), 0);
}

void TestLocalChangeMonitor::ignoresExcludedFilesAndDirectories()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    LocalChangeMonitor monitor(dir.path(), {QStringLiteral("*.tmp"), QStringLiteral(".git")});
    QSignalSpy changed(&monitor, &LocalChangeMonitor::localChanged);
    QString error;
    QVERIFY2(monitor.start(&error), qPrintable(error));

    QVERIFY(writeFile(dir.filePath(QStringLiteral("ignored.tmp")), "ignored"));
    QTest::qWait(1900);
    QCOMPARE(changed.count(), 0);

    QVERIFY(writeFile(dir.filePath(QStringLiteral(".git/config")), "ignored"));
    QTest::qWait(1900);
    QCOMPARE(changed.count(), 0);

    QVERIFY(writeFile(dir.filePath(QStringLiteral("watched.txt")), "watched"));
    QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 1, 5000);
}

void TestLocalChangeMonitor::suppressesDuringSyncAndResumes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    LocalChangeMonitor monitor(dir.path(), {});
    QSignalSpy changed(&monitor, &LocalChangeMonitor::localChanged);
    QString error;
    QVERIFY2(monitor.start(&error), qPrintable(error));

    monitor.setSuppressed(true);
    QVERIFY(writeFile(dir.filePath(QStringLiteral("while-syncing.txt")), "own write"));
    QTest::qWait(1900);
    QCOMPARE(changed.count(), 0);

    monitor.setSuppressed(false);
    QVERIFY(writeFile(dir.filePath(QStringLiteral("user-edit.txt")), "user write"));
    QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 1, 5000);
}

void TestLocalChangeMonitor::watchesNewAndRenamedDirectories()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    LocalChangeMonitor monitor(dir.path(), {});
    QSignalSpy changed(&monitor, &LocalChangeMonitor::localChanged);
    QString error;
    QVERIFY2(monitor.start(&error), qPrintable(error));

    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("newdir"))));
    QVERIFY(writeFile(dir.filePath(QStringLiteral("newdir/first.txt")), "first"));
    QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 1, 5000);

    changed.clear();
    QVERIFY(writeFile(dir.filePath(QStringLiteral("newdir/second.txt")), "second"));
    QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 1, 5000);

    QVERIFY(QDir(dir.path()).rename(QStringLiteral("newdir"), QStringLiteral("renamed")));
    QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 1, 5000);

    changed.clear();
    QVERIFY(writeFile(dir.filePath(QStringLiteral("renamed/third.txt")), "third"));
    QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 1, 5000);
}

} // namespace NutSync::Test
