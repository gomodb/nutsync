#pragma once

#include <QObject>

namespace NutSync::Test {
class TestRateLimiter : public QObject
{
    Q_OBJECT

private slots:
    void unlimitedByDefault();
    void enforcesWindowAndPersists();
    void manualStartsCanExceedQuota();
    void clearRemovesHistory();
};
} // namespace NutSync::Test
