#pragma once

#include <QObject>

namespace NutSync::Test {
class TestStateStore : public QObject
{
    Q_OBJECT

private slots:
    void rejectsFutureSchemaVersion();
};
} // namespace NutSync::Test
