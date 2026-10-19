#pragma once

#include <QObject>

namespace NutSync::Test {
class TestStateStore : public QObject
{
    Q_OBJECT

private slots:
    void persistsConflicts();
    void migratesVersionOne();
    void rejectsFutureSchemaVersion();
    void writesAndLoadsHistory();
    void migratesHistoryFromVersionThree();
};
} // namespace NutSync::Test
