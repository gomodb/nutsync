#pragma once

#include <QObject>

namespace NutSync::Test {
class TestNextcloud : public QObject
{
    Q_OBJECT

private slots:
    void webdavRoundTrip();
};
} // namespace NutSync::Test
