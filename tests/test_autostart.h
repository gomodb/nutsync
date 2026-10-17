#pragma once

#include <QObject>

namespace NutSync::Test {
class TestAutostart : public QObject
{
    Q_OBJECT

private slots:
    void disabledByDefault();
    void enablesAndDisablesDesktopEntry();
};
} // namespace NutSync::Test
