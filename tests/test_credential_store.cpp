#include "test_credential_store.h"

#include "core/credential_store.h"

#include <QTest>
#include <QUuid>

namespace NutSync::Test {

void TestCredentialStore::roundTripsAndClearsPassword()
{
    CredentialStore store;
    if (!store.isAvailable())
        QSKIP("System keychain is not available in this build");

    const QString key =
        QStringLiteral("test-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString error;
    if (!store.writePassword(key, QStringLiteral("secret-value"), &error))
        QSKIP(qPrintable(QStringLiteral("System keychain unavailable: %1").arg(error)));

    QCOMPARE(store.readPassword(key, &error), QStringLiteral("secret-value"));
    QVERIFY2(store.deletePassword(key, &error), qPrintable(error));
    QVERIFY(store.readPassword(key, &error).isEmpty());
}

} // namespace NutSync::Test
