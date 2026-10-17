#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace NutSync::PathUtils {

QString normalizeRelativePath(const QString &path, bool *ok = nullptr);
QString parentPath(const QString &path);
int pathDepth(const QString &path);
bool isSameOrChild(const QString &path, const QString &parent);
bool isExcluded(const QString &relativePath, const QStringList &patterns);
QString localFilePath(const QString &root, const QString &relativePath);
QUrl remoteUrl(const QUrl &base, const QString &relativePath);
QString conflictRelativePath(const QString &relativePath, const QDateTime &timestamp,
                             int attempt = 0);
QDateTime parseHttpDate(const QString &value);
QString normalizeEtag(const QString &value);
QString formatBytes(qint64 bytes);
QString nativePath(const QString &path);

} // namespace NutSync::PathUtils
