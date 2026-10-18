#pragma once

#include <QLocale>
#include <QString>

class QCoreApplication;
class QTranslator;

namespace NutSync::Language {

QString normalizePreference(const QString &preference);
QString resolve(const QString &preference, QLocale::Language systemLanguage);
QString resolve(const QString &preference);

bool installTranslator(QCoreApplication &application, const QString &language,
                       QTranslator *applicationTranslator, QTranslator *qtTranslator = nullptr);

} // namespace NutSync::Language
