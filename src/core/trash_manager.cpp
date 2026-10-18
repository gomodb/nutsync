#include "core/trash_manager.h"

#include "core/app_paths.h"
#include "core/path_utils.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>

namespace NutSync::TrashManager {

namespace {
QString destinationPath(const QString &batchRoot, const QString &relativePath)
{
    return QDir(batchRoot).filePath(relativePath);
}

bool copyDirectoryContents(const QString &source, const QString &destination, QString *error)
{
    QDir sourceDir(source);
    if (!QDir().mkpath(destination)) {
        if (error)
            *error = QObject::tr("无法创建回收站目录“%1”").arg(destination);
        return false;
    }
    const QFileInfoList entries = sourceDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot
                                                              | QDir::Hidden | QDir::System,
                                                          QDir::DirsFirst | QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (entry.isSymLink())
            continue;
        const QString target = QDir(destination).filePath(entry.fileName());
        if (entry.isDir()) {
            if (!copyDirectoryContents(entry.absoluteFilePath(), target, error))
                return false;
        } else if (entry.isFile()) {
            QFile::remove(target);
            if (!QFile::copy(entry.absoluteFilePath(), target)) {
                if (error)
                    *error = QObject::tr("无法复制到回收站“%1”").arg(target);
                return false;
            }
        }
    }
    return true;
}
} // namespace

QString pairDir(const QString &pairId)
{
    QString safeId;
    safeId.reserve(pairId.size());
    for (const QChar character : pairId) {
        safeId.append(character.isLetterOrNumber() || character == QLatin1Char('-')
                              || character == QLatin1Char('_') || character == QLatin1Char('.')
                          ? character
                          : QLatin1Char('_'));
    }
    if (safeId.isEmpty() || safeId == QLatin1String(".") || safeId == QLatin1String(".."))
        safeId = QStringLiteral("unknown");
    return QDir(AppPaths::trashDir()).filePath(safeId);
}

QString createBatch(const QString &pairId, int retentionDays, QString *error)
{
    cleanupOldBatches(pairId, retentionDays);
    const QString batch =
        QDir(pairDir(pairId))
            .filePath(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
    if (!QDir().mkpath(batch)) {
        if (error)
            *error = QObject::tr("无法创建回收站批次“%1”").arg(batch);
        return {};
    }
    QFile::setPermissions(batch,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return batch;
}

bool copyLocalPath(const QString &source, const QString &batchRoot, const QString &relativePath,
                   QString *error)
{
    const QFileInfo info(source);
    const QString destination = destinationPath(batchRoot, relativePath);
    if (info.isDir())
        return copyDirectoryContents(source, destination, error);
    if (!info.isFile()) {
        if (error)
            *error = QObject::tr("无法备份特殊文件“%1”").arg(source);
        return false;
    }
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
        if (error)
            *error =
                QObject::tr("无法创建回收站目录“%1”").arg(QFileInfo(destination).absolutePath());
        return false;
    }
    QFile::remove(destination);
    if (!QFile::copy(source, destination)) {
        if (error)
            *error = QObject::tr("无法复制到回收站“%1”").arg(destination);
        return false;
    }
    return true;
}

bool downloadRemoteFile(RemoteBackend &backend, const QString &remotePath, const QString &batchRoot,
                        const QString &relativePath, const CancelledCallback &cancelled,
                        QString *error)
{
    const QString destination = destinationPath(batchRoot, relativePath);
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
        if (error)
            *error =
                QObject::tr("无法创建回收站目录“%1”").arg(QFileInfo(destination).absolutePath());
        return false;
    }
    DownloadResult result;
    if (!backend.download(remotePath, destination, &result, error, cancelled))
        return false;
    return true;
}

void cleanupOldBatches(const QString &pairId, int retentionDays)
{
    if (retentionDays <= 0)
        return;
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-retentionDays);
    const QFileInfoList batches =
        QDir(pairDir(pairId)).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &batch : batches) {
        if (batch.lastModified() < cutoff)
            QDir(batch.absoluteFilePath()).removeRecursively();
    }
}

} // namespace NutSync::TrashManager
