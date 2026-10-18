#include "test_language.h"

#include "core/language.h"

#include <QLocale>
#include <QTest>

namespace NutSync::Test {

void TestLanguage::normalizesPreferences()
{
    QCOMPARE(Language::normalizePreference(QStringLiteral("en")), QStringLiteral("en"));
    QCOMPARE(Language::normalizePreference(QStringLiteral("zh")), QStringLiteral("zh_CN"));
    QCOMPARE(Language::normalizePreference(QStringLiteral("zh_CN")), QStringLiteral("zh_CN"));
    QCOMPARE(Language::normalizePreference(QStringLiteral("system")), QStringLiteral("system"));
    QCOMPARE(Language::normalizePreference(QStringLiteral("fr")), QStringLiteral("system"));
}

void TestLanguage::resolvesSystemLanguage()
{
    QCOMPARE(Language::resolve(QStringLiteral("system"), QLocale::Chinese),
             QStringLiteral("zh_CN"));
    QCOMPARE(Language::resolve(QStringLiteral("system"), QLocale::English),
             QStringLiteral("en"));
    QCOMPARE(Language::resolve(QStringLiteral("system"), QLocale::French),
             QStringLiteral("en"));
    QCOMPARE(Language::resolve(QString(), QLocale::French), QStringLiteral("en"));
}

void TestLanguage::explicitPreferenceWins()
{
    QCOMPARE(Language::resolve(QStringLiteral("en"), QLocale::Chinese), QStringLiteral("en"));
    QCOMPARE(Language::resolve(QStringLiteral("zh_CN"), QLocale::English),
             QStringLiteral("zh_CN"));
}

} // namespace NutSync::Test
