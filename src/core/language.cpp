#include "core/language.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QTranslator>

namespace NutSync::Language {

QString normalizePreference(const QString &preference)
{
    if (preference == QLatin1String("en"))
        return QStringLiteral("en");
    if (preference == QLatin1String("zh") || preference == QLatin1String("zh_CN"))
        return QStringLiteral("zh_CN");
    return QStringLiteral("system");
}

QString resolve(const QString &preference, QLocale::Language systemLanguage)
{
    const QString normalized = normalizePreference(preference);
    if (normalized == QLatin1String("en"))
        return QStringLiteral("en");
    if (normalized == QLatin1String("zh_CN"))
        return QStringLiteral("zh_CN");
    return systemLanguage == QLocale::Chinese ? QStringLiteral("zh_CN") : QStringLiteral("en");
}

QString resolve(const QString &preference)
{
    return resolve(preference, QLocale::system().language());
}

bool installTranslator(QCoreApplication &application, const QString &language,
                       QTranslator *applicationTranslator, QTranslator *qtTranslator)
{
    if (!applicationTranslator)
        return false;

    if (qtTranslator) {
        const QString translationsPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
        if (qtTranslator->load(QStringLiteral("qtbase_") + language, translationsPath))
            application.installTranslator(qtTranslator);
    }

    if (language != QLatin1String("en"))
        return true;

    if (!applicationTranslator->load(QStringLiteral(":/i18n/nutsync_en.qm")))
        return false;

    application.installTranslator(applicationTranslator);
    return true;
}

} // namespace NutSync::Language
