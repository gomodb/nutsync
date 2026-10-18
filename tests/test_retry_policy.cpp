#include "test_retry_policy.h"

#include "core/retry_policy.h"

#include <QTest>

namespace NutSync::Test {

void TestRetryPolicy::classifiesPermanentErrors()
{
    SyncSummary summary;
    summary.errorDetails = {QStringLiteral("认证失败（HTTP 401）")};
    QVERIFY(RetryPolicy::hasPermanentHttpError(summary));

    summary.errorDetails = {QStringLiteral("远程服务器错误（HTTP 503）")};
    QVERIFY(!RetryPolicy::hasPermanentHttpError(summary));

    summary.errorDetails = {QStringLiteral("连接超时")};
    QVERIFY(!RetryPolicy::hasPermanentHttpError(summary));

    summary.errorDetails = {QStringLiteral("HTTP 429"), QStringLiteral("HTTP 412")};
    QVERIFY(RetryPolicy::hasPermanentHttpError(summary));
}

void TestRetryPolicy::computesBackoff()
{
    QCOMPARE(RetryPolicy::backoffSeconds(30, 1), 30);
    QCOMPARE(RetryPolicy::backoffSeconds(30, 2), 60);
    QCOMPARE(RetryPolicy::backoffSeconds(30, 5), 480);
    QCOMPARE(RetryPolicy::backoffSeconds(30, 10), 600);
    QCOMPARE(RetryPolicy::backoffSeconds(0, 3), 0);
}

} // namespace NutSync::Test
