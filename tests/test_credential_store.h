#pragma once

#include <QObject>

namespace NutSync::Test {
class TestCredentialStore : public QObject
{
    Q_OBJECT

private slots:
    void roundTripsAndClearsPassword();
};
} // namespace NutSync::Test
