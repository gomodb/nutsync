#pragma once

#include <QObject>

namespace NutSync::Test {
class TestDiagnostics : public QObject
{
    Q_OBJECT

private slots:
    void redactsSecrets();
    void readsLogTail();
};
} // namespace NutSync::Test
