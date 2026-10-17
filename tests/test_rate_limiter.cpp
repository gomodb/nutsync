#include "test_rate_limiter.h"

#include "core/sync_rate_limiter.h"

#include <QDateTime>
#include <QFileInfo>
#include <QTimeZone>
#include <QTest>
#include <QTemporaryDir>

namespace NutSync::Test {

namespace {
SyncPair limitedPair(int windowMinutes = 60, int maxRequests = 2)
{
    SyncPair pair;
    pair.id = QStringLiteral("rate-pair");
    pair.name = QStringLiteral("Rate Pair");
    pair.localPath = QStringLiteral("/tmp/local");
    pair.remoteUrl = QStringLiteral("https://example.com/dav/");
    pair.rateLimitEnabled = true;
    pair.rateLimitWindowMinutes = windowMinutes;
    pair.rateLimitMaxRequests = maxRequests;
    return pair;
}
} // namespace

void TestRateLimiter::unlimitedByDefault()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SyncRateLimiter limiter(dir.filePath(QStringLiteral("rate.json")));
    SyncPair pair = limitedPair();
    pair.rateLimitEnabled = false;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QVERIFY(limiter.isAllowed(pair, now));
    QString error;
    QVERIFY(limiter.recordStart(pair, now, &error));
    QCOMPARE(limiter.requestCount(pair, now), 0);
    QVERIFY(!QFileInfo::exists(limiter.storagePath()));
    QVERIFY(error.isEmpty());
}

void TestRateLimiter::enforcesWindowAndPersists()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("rate.json"));
    SyncRateLimiter limiter(path);
    SyncPair pair = limitedPair(60, 2);
    const QDateTime start(QDate(2026, 10, 18), QTime(2, 0), QTimeZone::utc());

    QVERIFY(limiter.isAllowed(pair, start));
    QString error;
    QVERIFY2(limiter.recordStart(pair, start, &error), qPrintable(error));
    QVERIFY(limiter.isAllowed(pair, start.addSecs(60)));
    QVERIFY2(limiter.recordStart(pair, start.addSecs(60), &error), qPrintable(error));

    QDateTime next;
    QVERIFY(!limiter.isAllowed(pair, start.addSecs(120), &next));
    QVERIFY(next.isValid());
    QCOMPARE(next, start.addSecs(3600));
    QCOMPARE(limiter.requestCount(pair, start.addSecs(120)), 2);
    QVERIFY(QFileInfo::exists(path));

    SyncRateLimiter reloaded(path);
    QVERIFY2(reloaded.load(&error), qPrintable(error));
    QCOMPARE(reloaded.requestCount(pair, start.addSecs(120)), 2);
    QVERIFY(!reloaded.isAllowed(pair, start.addSecs(120), &next));
    QVERIFY(reloaded.isAllowed(pair, start.addSecs(3600), &next));
    QCOMPARE(reloaded.requestCount(pair, start.addSecs(3600)), 1);
}

void TestRateLimiter::manualStartsCanExceedQuota()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SyncRateLimiter limiter(dir.filePath(QStringLiteral("rate.json")));
    SyncPair pair = limitedPair(10, 1);
    const QDateTime start = QDateTime::currentDateTimeUtc();
    QString error;
    QVERIFY(limiter.recordStart(pair, start, &error));
    QVERIFY(!limiter.isAllowed(pair, start.addSecs(5)));
    // A manual sync bypasses isAllowed(), but recordStart() still consumes quota.
    QVERIFY2(limiter.recordStart(pair, start.addSecs(5), &error), qPrintable(error));
    QCOMPARE(limiter.requestCount(pair, start.addSecs(6)), 2);
    QVERIFY(!limiter.isAllowed(pair, start.addSecs(6)));
    QVERIFY(limiter.isAllowed(pair, start.addSecs(606)));
}

void TestRateLimiter::clearRemovesHistory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SyncRateLimiter limiter(dir.filePath(QStringLiteral("rate.json")));
    SyncPair pair = limitedPair(60, 1);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QString error;
    QVERIFY(limiter.recordStart(pair, now, &error));
    QVERIFY(!limiter.isAllowed(pair, now));
    QVERIFY2(limiter.clear(pair.id, &error), qPrintable(error));
    QVERIFY(limiter.isAllowed(pair, now));
    QCOMPARE(limiter.requestCount(pair, now), 0);
}

} // namespace NutSync::Test
