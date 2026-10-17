#pragma once

#include <QObject>

namespace NutSync::Test {
class TestConfigStore : public QObject
{
    Q_OBJECT

private slots:
    void stripsCredentialsEmbeddedInUrls();
    void roundTripsPairSettings();
};
} // namespace NutSync::Test
