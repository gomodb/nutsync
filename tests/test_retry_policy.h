#pragma once

#include <QObject>

namespace NutSync::Test {
class TestRetryPolicy : public QObject
{
    Q_OBJECT

private slots:
    void classifiesPermanentErrors();
    void computesBackoff();
};
} // namespace NutSync::Test
