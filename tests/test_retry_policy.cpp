#include "test_retry_policy.h"

#include "core/retry_policy.h"

#include <QTest>

namespace NutSync::Test {

void TestRetryPolicy::classifiesPermanentErrors()
{
    QVERIFY(RetryPolicy::isRetryable(RemoteErrorClass::Network));
    QVERIFY(RetryPolicy::isRetryable(RemoteErrorClass::Timeout));
    QVERIFY(RetryPolicy::isRetryable(RemoteErrorClass::RateLimited));
    QVERIFY(RetryPolicy::isRetryable(RemoteErrorClass::Server));

    QVERIFY(!RetryPolicy::isRetryable(RemoteErrorClass::Authentication));
    QVERIFY(!RetryPolicy::isRetryable(RemoteErrorClass::Permission));
    QVERIFY(!RetryPolicy::isRetryable(RemoteErrorClass::NotFound));
    QVERIFY(!RetryPolicy::isRetryable(RemoteErrorClass::Conflict));
    QVERIFY(!RetryPolicy::isRetryable(RemoteErrorClass::Redirect));
    QVERIFY(!RetryPolicy::isRetryable(RemoteErrorClass::Protocol));
    QVERIFY(!RetryPolicy::isRetryable(RemoteErrorClass::LocalIo));
    QVERIFY(!RetryPolicy::isRetryable(RemoteErrorClass::Cancelled));
}

} // namespace NutSync::Test
