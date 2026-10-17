#include "test_path_utils.h"

#include "core/path_utils.h"

#include <QTest>
#include <QTimeZone>
#include <QUrl>

namespace NutSync::Test {

void TestPathUtils::normalizesRelativePaths()
{
    bool ok = false;
    QCOMPARE(PathUtils::normalizeRelativePath(QStringLiteral("a//b/./c"), &ok),
             QStringLiteral("a/b/c"));
    QVERIFY(ok);
    QCOMPARE(PathUtils::normalizeRelativePath(QStringLiteral("/leading/"), &ok),
             QStringLiteral("leading"));
    QVERIFY(ok);
    PathUtils::normalizeRelativePath(QStringLiteral("a/../b"), &ok);
    QVERIFY(!ok);

    // A backslash is a valid Linux filename character and must be preserved.
    QCOMPARE(PathUtils::normalizeRelativePath(QStringLiteral("a\\b"), &ok),
             QStringLiteral("a\\b"));
    QVERIFY(ok);
}

void TestPathUtils::computesPathRelations()
{
    QCOMPARE(PathUtils::parentPath(QStringLiteral("a/b/c.txt")), QStringLiteral("a/b"));
    QVERIFY(PathUtils::parentPath(QStringLiteral("top")).isEmpty());
    QCOMPARE(PathUtils::pathDepth(QStringLiteral("a/b/c")), 3);
    QCOMPARE(PathUtils::pathDepth(QString()), 0);
    QVERIFY(PathUtils::isSameOrChild(QStringLiteral("a/b"), QStringLiteral("a")));
    QVERIFY(PathUtils::isSameOrChild(QStringLiteral("a"), QStringLiteral("a")));
    QVERIFY(!PathUtils::isSameOrChild(QStringLiteral("ab"), QStringLiteral("a")));
    QVERIFY(PathUtils::isSameOrChild(QStringLiteral("anything"), QString()));
}

void TestPathUtils::matchesExcludePatterns()
{
    const QStringList patterns = {
        QStringLiteral(".git"),
        QStringLiteral("*.tmp"),
        QStringLiteral("node_modules"),
        QStringLiteral("cache/"),
        QStringLiteral("docs/private/*.txt"),
    };
    QVERIFY(PathUtils::isExcluded(QStringLiteral(".git/config"), patterns));
    QVERIFY(PathUtils::isExcluded(QStringLiteral("src/.git"), patterns));
    QVERIFY(PathUtils::isExcluded(QStringLiteral("src/a.tmp"), patterns));
    QVERIFY(PathUtils::isExcluded(QStringLiteral("node_modules/pkg/index.js"), patterns));
    QVERIFY(PathUtils::isExcluded(QStringLiteral("cache/data.bin"), patterns));
    QVERIFY(PathUtils::isExcluded(QStringLiteral("docs/private/note.txt"), patterns));
    QVERIFY(!PathUtils::isExcluded(QStringLiteral("docs/public/note.txt"), patterns));
    QVERIFY(!PathUtils::isExcluded(QStringLiteral("src/main.cpp"), patterns));
}

void TestPathUtils::buildsRemoteUrls()
{
    const QUrl base(QStringLiteral("https://example.com/dav/folder"));
    const QUrl fileUrl = PathUtils::remoteUrl(base, QStringLiteral("sub dir/你好.txt"));
    QCOMPARE(fileUrl.host(), QStringLiteral("example.com"));
    QVERIFY(fileUrl.path().endsWith(QStringLiteral("/sub dir/你好.txt")));
    QVERIFY(fileUrl.toEncoded().contains("%20"));
    QVERIFY(fileUrl.toEncoded().contains("%E4%BD%A0"));

    const QUrl directoryUrl = PathUtils::remoteUrl(base, {});
    QCOMPARE(directoryUrl.path(), QStringLiteral("/dav/folder/"));

    const QUrl queryBase(QStringLiteral("https://example.com/dav?token=abc"));
    const QUrl withQuery = PathUtils::remoteUrl(queryBase, QStringLiteral("a.txt"));
    QCOMPARE(withQuery.query(), QStringLiteral("token=abc"));
    QCOMPARE(withQuery.path(), QStringLiteral("/dav/a.txt"));
}

void TestPathUtils::buildsConflictNames()
{
    const QDateTime timestamp(QDate(2026, 10, 18), QTime(2, 15, 0), QTimeZone::UTC);
    QCOMPARE(PathUtils::conflictRelativePath(QStringLiteral("docs/report.txt"), timestamp),
             QStringLiteral("docs/report.conflict-20261018-021500.txt"));
    QCOMPARE(PathUtils::conflictRelativePath(QStringLiteral(".bashrc"), timestamp),
             QStringLiteral(".bashrc.conflict-20261018-021500"));
    QCOMPARE(PathUtils::conflictRelativePath(QStringLiteral("notes"), timestamp, 2),
             QStringLiteral("notes.conflict-20261018-021500-2"));
}

void TestPathUtils::parsesHttpDates()
{
    const QDateTime date =
        PathUtils::parseHttpDate(QStringLiteral("Wed, 21 Oct 2015 07:28:00 GMT"));
    QVERIFY(date.isValid());
    QCOMPARE(date.toUTC().toString(Qt::ISODate), QStringLiteral("2015-10-21T07:28:00Z"));
    QVERIFY(!PathUtils::parseHttpDate(QStringLiteral("not a date")).isValid());
}

void TestPathUtils::formatsSizes()
{
    QCOMPARE(PathUtils::formatBytes(0), QStringLiteral("0 B"));
    QCOMPARE(PathUtils::formatBytes(1024), QStringLiteral("1.0 KiB"));
    QCOMPARE(PathUtils::formatBytes(10 * 1024 * 1024), QStringLiteral("10 MiB"));
}

} // namespace NutSync::Test
