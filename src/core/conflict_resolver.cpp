#include "core/conflict_resolver.h"

#include "core/path_utils.h"
#include "core/sync_state_store.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSaveFile>

namespace NutSync {

namespace {
bool replaceLocalFile(const QString &source, const QString &destination, QString *error)
{
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("无法读取冲突副本“%1”：%2").arg(source, input.errorString());
        return false;
    }
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QObject::tr("无法写入本地文件“%1”：%2").arg(destination, output.errorString());
        return false;
    }
    while (!input.atEnd()) {
        const QByteArray chunk = input.read(1024 * 1024);
        if (chunk.isEmpty())
            break;
        if (output.write(chunk) != chunk.size()) {
            if (error)
                *error = QObject::tr("写入本地文件失败：%1").arg(output.errorString());
            return false;
        }
    }
    if (!output.commit()) {
        if (error)
            *error = QObject::tr("保存本地文件失败：%1").arg(output.errorString());
        return false;
    }
    return true;
}

bool removeLocalFile(const QString &path, QString *error)
{
    if (!QFileInfo::exists(path))
        return true;
    if (!QFile::remove(path)) {
        if (error)
            *error = QObject::tr("无法删除本地冲突副本“%1”").arg(path);
        return false;
    }
    return true;
}

bool removeRemoteFile(RemoteBackend &backend, const QString &path, QString *error)
{
    return backend.remove(path, false, {}, error, {});
}

} // namespace

ConflictResolutionResult resolveConflict(const SyncPair &pair, const ConflictRecord &record,
                                         ConflictResolution resolution, RemoteBackend &backend,
                                         const QString &stateDatabasePath)
{
    ConflictResolutionResult result;
    if (record.path.isEmpty() || record.copyPath.isEmpty()) {
        result.error = QObject::tr("冲突记录缺少原始路径或副本路径");
        return result;
    }

    const QString localOriginal = PathUtils::localFilePath(pair.localPath, record.path);
    const QString localCopy = PathUtils::localFilePath(pair.localPath, record.copyPath);
    SyncStateStore store(stateDatabasePath);
    QString error;
    if (!store.open(&error)) {
        result.error = error;
        return result;
    }

    if (resolution == ConflictResolution::KeepRemote) {
        if (!QFileInfo(localCopy).isFile()) {
            result.error = QObject::tr("冲突副本不存在：%1").arg(localCopy);
            return result;
        }
        if (!replaceLocalFile(localCopy, localOriginal, &error)) {
            result.error = error;
            return result;
        }

        RemoteEntry current;
        bool exists = false;
        if (!backend.stat(record.path, &current, &exists, &error)) {
            result.error = error;
            return result;
        }
        const QString expectedEtag = exists ? current.etag : QStringLiteral("*");
        if (!backend.upload(localOriginal, record.path, expectedEtag, &error, {})) {
            result.error = error;
            return result;
        }
        RemoteEntry uploaded;
        if (!backend.stat(record.path, &uploaded, &exists, &error) || !exists) {
            result.error = error.isEmpty() ? QObject::tr("无法读取远程文件状态") : error;
            return result;
        }

        StateEntry entry;
        QFileInfo localInfo(localOriginal);
        entry.path = record.path;
        entry.type = EntryType::File;
        entry.localSize = localInfo.size();
        entry.localMtimeMs = localInfo.lastModified().toMSecsSinceEpoch();
        entry.remoteSize = uploaded.size;
        entry.remoteMtimeMs = uploaded.mtimeMs;
        entry.remoteEtag = uploaded.etag;
        entry.syncedAtMs = QDateTime::currentMSecsSinceEpoch();
        if (!store.upsert(pair.id, entry, &error)) {
            result.error = error;
            return result;
        }
    }

    if (!removeLocalFile(localCopy, &error)) {
        result.error = error;
        return result;
    }
    if (!removeRemoteFile(backend, record.copyPath, &error)) {
        result.error = error;
        return result;
    }
    if (!store.removePath(pair.id, record.copyPath, &error)
        || !store.removeConflict(pair.id, record.path, record.copyPath, &error)) {
        result.error = error;
        return result;
    }
    result.success = true;
    return result;
}

} // namespace NutSync
