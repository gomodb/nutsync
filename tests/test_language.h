#pragma once

#include <QObject>

namespace NutSync::Test {
class TestLanguage : public QObject
{
    Q_OBJECT

private slots:
    void normalizesPreferences();
    void resolvesSystemLanguage();
    void explicitPreferenceWins();
};
} // namespace NutSync::Test
