#pragma once

#include <QObject>

namespace NutSync::Test {
class TestConflictResolver : public QObject
{
    Q_OBJECT

private slots:
    void keepsRemoteVersion();
    void keepsLocalVersion();
};
} // namespace NutSync::Test
