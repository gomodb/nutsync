#include "core/path_utils.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QTimeZone>

namespace NutSync::PathUtils {

QString normalizeRelativePath(const QString &path, bool *ok)
{
    const QString input = path;
    const QStringList rawParts = input.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QStringList parts;
    parts.reserve(rawParts.size());
    for (const QString &part : rawParts) {
        if (part == QLatin1String("."))
            continue;
        if (part == QLatin1String("..")) {
            if (ok)
                *ok = false;
            return {};
        }
        parts.append(part);
    }
    if (ok)
        *ok = true;
    return parts.join(QLatin1Char('/'));
}

QString parentPath(const QString &path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : path.left(slash);
}

int pathDepth(const QString &path)
{
    if (path.isEmpty())
        return 0;
    return path.count(QLatin1Char('/')) + 1;
}

bool isSameOrChild(const QString &path, const QString &parent)
{
    if (parent.isEmpty())
        return true;
    return path == parent || path.startsWith(parent + QLatin1Char('/'));
}

bool isExcluded(const QString &relativePath, const QStringList &patterns)
{
    if (relativePath.isEmpty() || patterns.isEmpty())
        return false;

    const QString name = relativePath.section(QLatin1Char('/'), -1);
    const QStringList segments = relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);

    for (QString pattern : patterns) {
        pattern = pattern.trimmed();
        if (pattern.isEmpty() || pattern.startsWith(QLatin1Char('#')))
            continue;
        while (pattern.startsWith(QLatin1Char('/')))
            pattern.remove(0, 1);
        while (pattern.endsWith(QLatin1Char('/')))
            pattern.chop(1);
        if (pattern.isEmpty())
            continue;

        if (QDir::match(pattern, relativePath) || QDir::match(pattern, name))
            return true;

        // A pattern such as ".git" excludes that directory at any depth.
        if (!pattern.contains(QLatin1Char('/'))) {
            for (const QString &segment : segments) {
                if (QDir::match(pattern, segment))
                    return true;
            }
        }
    }
    return false;
}

QString localFilePath(const QString &root, const QString &relativePath)
{
    if (relativePath.isEmpty())
        return QDir::cleanPath(root);
    return QDir::cleanPath(QDir(root).filePath(relativePath));
}

QUrl remoteUrl(const QUrl &base, const QString &relativePath)
{
    QUrl url = base;
    QString basePath = url.path();
    if (!basePath.endsWith(QLatin1Char('/')))
        basePath.append(QLatin1Char('/'));
    if (!relativePath.isEmpty()) {
        QString suffix = relativePath;
        while (suffix.startsWith(QLatin1Char('/')))
            suffix.remove(0, 1);
        basePath.append(suffix);
    }
    url.setPath(basePath);
    return url;
}

QString conflictRelativePath(const QString &relativePath, const QDateTime &timestamp,
                             int attempt)
{
    const QString date = timestamp.toUTC().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString parent = parentPath(relativePath);
    const QString fileName = relativePath.section(QLatin1Char('/'), -1);

    QString stem = fileName;
    QString suffix;
    const int dot = fileName.lastIndexOf(QLatin1Char('.'));
    if (dot > 0 && dot < fileName.size() - 1) {
        stem = fileName.left(dot);
        suffix = fileName.mid(dot + 1);
    }

    QString conflictName = QStringLiteral("%1.conflict-%2").arg(stem, date);
    if (attempt > 0)
        conflictName += QStringLiteral("-%1").arg(attempt);
    if (!suffix.isEmpty())
        conflictName += QLatin1Char('.') + suffix;

    return parent.isEmpty() ? conflictName : parent + QLatin1Char('/') + conflictName;
}

QDateTime parseHttpDate(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty())
        return {};

    QDateTime date = QDateTime::fromString(trimmed, Qt::RFC2822Date);
    if (!date.isValid()) {
        static const QLocale cLocale = QLocale::c();
        date = cLocale.toDateTime(trimmed, QStringLiteral("ddd, dd MMM yyyy HH:mm:ss 'GMT'"));
    }
    if (!date.isValid())
        date = QDateTime::fromString(trimmed, Qt::ISODate);
    if (!date.isValid())
        return {};

    if (date.timeSpec() == Qt::LocalTime)
        date.setTimeZone(QTimeZone::utc());
    return date.toUTC();
}

QString normalizeEtag(const QString &value)
{
    return value.trimmed();
}

QString formatBytes(qint64 bytes)
{
    if (bytes < 0)
        return QStringLiteral("—");
    static const QStringList units = {
        QStringLiteral("B"), QStringLiteral("KiB"), QStringLiteral("MiB"),
        QStringLiteral("GiB"), QStringLiteral("TiB")};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit + 1 < units.size()) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0)
        return QStringLiteral("%1 B").arg(bytes);
    return QStringLiteral("%1 %2").arg(value, 0, 'f', value < 10.0 ? 1 : 0).arg(units.at(unit));
}

QString nativePath(const QString &path)
{
    return QDir::toNativeSeparators(path);
}

} // namespace NutSync::PathUtils
