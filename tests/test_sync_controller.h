#pragma once

#include <QObject>

namespace NutSync::Test {
class TestSyncController : public QObject
{
    Q_OBJECT

private slots:
    void queuedSyncRunsInWorkerAndReportsSummary();
};
} // namespace NutSync::Test
