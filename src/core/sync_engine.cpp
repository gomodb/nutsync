#include "core/sync_engine.h"

#include "core/app_paths.h"
#include "core/path_utils.h"
#include "core/sync_planner.h"
#include "core/sync_state_store.h"
#include "core/trash_manager.h"
#include "core/webdav_backend.h"

#include <QCryptographicHash>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QTimer>
#include <atomic>
#include <QtConcurrent>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QMutexLocker>
#include <QUuid>
#include <QUrl>

#include <algorithm>
#include <cstdlib>
#include <exception>

namespace NutSync {

namespace {

constexpr qint64 kRemoteMtimeToleranceMs = 2000;
constexpr int kMaxErrors = 100;

bool sameRemoteMetadata(const RemoteEntry &left, const RemoteEntry &right)
{
    if (left.type != right.type)
        return false;
    if (left.type == EntryType::Directory)
        return true;
    if (!left.etag.isEmpty() && !right.etag.isEmpty())
        return PathUtils::normalizeEtag(left.etag) == PathUtils::normalizeEtag(right.etag);
    if (left.size != right.size)
        return false;
    if (left.mtimeMs > 0 && right.mtimeMs > 0) {
        return std::llabs(left.mtimeMs - right.mtimeMs) <= kRemoteMtimeToleranceMs;
    }
    return true;
}

QString hashFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("无法读取“%1”：%2").arg(path, file.errorString());
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        if (error)
            *error = QObject::tr("读取“%1”时出错：%2").arg(path, file.errorString());
        return {};
    }
    return QString::fromLatin1(hash.result().toHex());
}

QString uniqueTemporaryPath()
{
    const QString cache = AppPaths::cacheDir();
    QDir().mkpath(cache);
    return QDir(cache).filePath(
        QStringLiteral("remote-%1.tmp").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
}

} // namespace

SyncEngine::SyncEngine(SyncPair pair, QString stateDatabasePath, QObject *parent)
    : QObject(parent)
    , m_pair(std::move(pair))
    , m_stateDatabasePath(stateDatabasePath.isEmpty() ? AppPaths::stateDatabase()
                                                      : std::move(stateDatabasePath))
{
    qRegisterMetaType<SyncSummary>("NutSync::SyncSummary");
    qRegisterMetaType<SyncPlan>("NutSync::SyncPlan");
}

void SyncEngine::setBackendFactory(BackendFactory factory)
{
    m_backendFactory = std::move(factory);
}

void SyncEngine::setRetryPolicy(int retryCount, int maxDelayMs)
{
    m_retryCount = qBound(0, retryCount, 5);
    m_retryMaxDelayMs = qBound(1000, maxDelayMs, 600000);
    m_retryBaseDelayMs = qMin(1000, m_retryMaxDelayMs);
}

void SyncEngine::setTransferLimit(int kbps)
{
    m_transferLimitKbps = qBound(0, kbps, 102400);
}

void SyncEngine::setParallelTransfers(int count)
{
    m_parallelTransfers = qBound(1, count, 4);
}

void SyncEngine::setScanConcurrency(int count)
{
    m_scanConcurrency = qBound(1, count, 8);
}

void SyncEngine::requestCancel()
{
    m_cancelled.storeRelaxed(1);
}

SyncSummary SyncEngine::lastSummary() const
{
    const QMutexLocker locker(&m_summaryMutex);
    return m_lastSummary;
}

void SyncEngine::emitLog(const QString &message, const QString &path)
{
    const QString line = path.isEmpty() ? message : QStringLiteral("%1：%2").arg(message, path);
    qInfo().noquote() << "[sync]" << line;
    emit logMessage(line);
}

void SyncEngine::addError(SyncSummary *summary, const QString &message)
{
    ++summary->errors;
    const RemoteErrorClass errorClass =
        m_backend && m_backend->lastErrorClass() != RemoteErrorClass::None
        ? m_backend->lastErrorClass()
        : RemoteErrorClass::Other;
    if (!summary->errorClasses.contains(errorClass))
        summary->errorClasses.append(errorClass);
    if (summary->errorDetails.size() < kMaxErrors)
        summary->errorDetails.append(message);
    qWarning().noquote() << "[sync]" << message;
    emit logMessage(QStringLiteral("错误：%1").arg(message));
}

std::unique_ptr<RemoteBackend> SyncEngine::createBackend(QString *error)
{
    if (m_backendFactory) {
        try {
            return m_backendFactory();
        } catch (const std::exception &exception) {
            if (error)
                *error = tr("创建远程后端失败：%1").arg(QString::fromUtf8(exception.what()));
            return {};
        }
    }

    if (m_pair.backendType != QLatin1String("webdav")) {
        if (error)
            *error = tr("不支持的同步后端类型：%1").arg(m_pair.backendType);
        return {};
    }
    const QUrl url(m_pair.remoteUrl.trimmed());
    if (!url.isValid() || url.host().isEmpty()
        || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))) {
        if (error)
            *error = tr("WebDAV 地址无效，必须以 http:// 或 https:// 开头");
        return {};
    }
    WebDavOptions options;
    options.baseUrl = url;
    options.username = m_pair.username;
    options.password = m_pair.password;
    options.authMode = m_pair.authMode;
    options.clientCertificatePath = m_pair.clientCertificatePath;
    options.clientKeyPath = m_pair.clientKeyPath;
    options.chunkedUpload = m_pair.chunkedUpload;
    options.chunkSizeMb = m_pair.chunkSizeMb;
    options.includePaths = m_pair.includePaths;
    options.scanConcurrency = m_scanConcurrency;
    options.oauthRefreshToken = m_pair.oauthRefreshToken;
    options.oauthTokenUrl = m_pair.oauthTokenUrl;
    options.oauthClientId = m_pair.oauthClientId;
    options.oauthExpiresAtMs = m_pair.oauthExpiresAtMs;
    options.tokenRefreshCallback = [this](const QString &refreshToken, qint64 expiresAtMs) {
        emit oauthTokensRefreshed(refreshToken, expiresAtMs);
    };
    options.retryCount = m_retryCount;
    options.retryBaseDelayMs = m_retryBaseDelayMs;
    options.retryMaxDelayMs = m_retryMaxDelayMs;
    options.transferLimitBytesPerSecond = m_transferLimitKbps * 1024;
    if (options.username.isEmpty() && !url.userName().isEmpty()) {
        options.username = url.userName();
        options.password = url.password();
    }
    options.baseUrl.setUserInfo({});
    return std::make_unique<WebDavBackend>(std::move(options));
}

bool SyncEngine::prepareTransferJob(const SyncAction &action, int index, TransferJob *job,
                                    bool *skipped, QString *error)
{
    *skipped = false;
    LocalEntry current;
    bool exists = false;
    statLocal(action.path, &current, &exists);
    if (action.local.isValid()) {
        if (!exists || current.type != action.local.type
            || (current.type == EntryType::File
                && (current.size != action.local.size
                    || current.mtimeMs != action.local.mtimeMs))) {
            if (error)
                *error = tr("本地文件在开始同步后发生变化，将在下次同步处理");
            return false;
        }
    } else if (exists) {
        if (error)
            *error = tr("本地路径在开始同步后发生变化，将在下次同步处理");
        return false;
    }

    job->index = index;
    job->action = action;
    if (action.kind == ActionKind::Upload) {
        QString etag;
        if (!verifyRemoteUnchanged(action, error, &etag))
            return false;
        const QString absolute = PathUtils::localFilePath(m_pair.localPath, action.path);
        if (action.remote.isValid()) {
            QString compareError;
            const bool equal = contentsEqual(absolute, action.remote, nullptr, &compareError);
            if (!compareError.isEmpty()) {
                emitLog(tr("检查远端内容失败，继续上传：%1").arg(compareError), action.path);
            } else if (equal) {
                if (!recordSynced(action.path, error))
                    return false;
                *skipped = true;
                return true;
            }
        }
        job->localFile = absolute;
        job->expectedEtag = action.remote.isValid() ? etag : QStringLiteral("*");
        job->bytes = action.local.size;
        emitLog(tr("上传"), action.path);
    } else {
        if (!ensureLocalParent(action.path, error))
            return false;
        job->localFile = PathUtils::localFilePath(m_pair.localPath, action.path);
        job->bytes = action.remote.size;
        emitLog(tr("下载"), action.path);
    }
    return true;
}

void SyncEngine::executeTransferBatch(const QList<TransferJob> &jobs, SyncSummary *summary)
{
    QThreadPool pool;
    pool.setMaxThreadCount(qMin(m_parallelTransfers, jobs.size()));
    std::atomic<qint64> transferred{0};
    std::atomic<int> finished{0};
    QThreadStorage<std::unique_ptr<RemoteBackend> *> backendStorage;
    QFuture<TransferOutcome> future = QtConcurrent::mapped(
        &pool, jobs, [this, &transferred, &finished, &backendStorage](const TransferJob &job) {
            TransferOutcome outcome;
            outcome.index = job.index;
            QString error;
            if (!backendStorage.localData()) {
                auto created =
                    std::make_unique<std::unique_ptr<RemoteBackend>>(createBackend(&error));
                if (!*created) {
                    outcome.error = error;
                    return outcome;
                }
                backendStorage.setLocalData(created.release());
            }
            RemoteBackend *backend = backendStorage.localData()->get();
            if (!backend) {
                outcome.error = tr("无法创建远程后端");
                return outcome;
            }
            backend->setTransferProgress([&transferred](qint64 received, qint64) {
                transferred.fetch_add(received, std::memory_order_relaxed);
            });
            const CancelledCallback cancelled = [this]() {
                return m_cancelled.loadRelaxed() != 0;
            };
            if (job.action.kind == ActionKind::Upload) {
                outcome.success = backend->upload(job.localFile, job.action.path, job.expectedEtag,
                                                  &error, cancelled);
            } else {
                DownloadResult result;
                outcome.success =
                    backend->download(job.action.path, job.localFile, &result, &error, cancelled);
            }
            outcome.error = error;
            finished.fetch_add(1, std::memory_order_relaxed);
            return outcome;
        });
    QFutureWatcher<TransferOutcome> watcher;
    QTimer progressTimer;
    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcher<TransferOutcome>::finished, &loop,
                     &QEventLoop::quit);
    QObject::connect(&progressTimer, &QTimer::timeout, &loop, [&]() {
        const qint64 bytes =
            qMin(m_bytesDone + transferred.load(std::memory_order_relaxed), m_bytesTotal);
        emit progress(m_completedActions + finished.load(std::memory_order_relaxed), m_totalActions,
                      bytes, m_bytesTotal, {});
    });
    watcher.setFuture(future);
    progressTimer.start(250);
    if (!future.isFinished())
        loop.exec();
    progressTimer.stop();
    const QList<TransferOutcome> outcomes = future.results();
    for (const TransferOutcome &outcome : outcomes) {
        if (outcome.index < 0 || outcome.index >= jobs.size())
            continue;
        const TransferJob &job = jobs.at(outcome.index);
        if (!outcome.success) {
            if (!m_cancelled.loadRelaxed()) {
                addError(summary, tr("“%1”：%2").arg(job.action.path, outcome.error));
                if (!summary->failedPaths.contains(job.action.path))
                    summary->failedPaths.append(job.action.path);
            }
        } else {
            QString stateError;
            if (!recordSynced(job.action.path, &stateError)) {
                addError(summary, tr("“%1”：%2").arg(job.action.path, stateError));
                if (!summary->failedPaths.contains(job.action.path))
                    summary->failedPaths.append(job.action.path);
            } else if (job.action.kind == ActionKind::Upload) {
                ++summary->uploaded;
            } else {
                ++summary->downloaded;
            }
        }
        m_bytesDone += job.bytes;
        ++m_completedActions;
        emit progress(m_completedActions, m_totalActions, m_bytesDone, m_bytesTotal,
                      job.action.path);
    }
}

void SyncEngine::run()
{
    QElapsedTimer elapsed;
    elapsed.start();

    SyncSummary summary;
    summary.pairId = m_pair.id;
    summary.pairName = m_pair.name;
    emit started();
    if (m_pair.syncMode == SyncMode::LocalToRemote)
        m_pair.conflictPolicy = ConflictPolicy::PreferLocal;
    else if (m_pair.syncMode == SyncMode::RemoteToLocal)
        m_pair.conflictPolicy = ConflictPolicy::PreferRemote;
    emitLog(tr("开始同步“%1”").arg(m_pair.name));

    QString fatalError;
    QString settingsError;
    SyncPlan plan;
    bool planBuilt = false;
    m_remoteSnapshot.clear();
    m_trashBatchPath.clear();

    do {
        if (m_cancelled.loadRelaxed()) {
            summary.cancelled = true;
            break;
        }
        if (m_pair.localPath.isEmpty() || m_pair.remoteUrl.isEmpty()) {
            fatalError = tr("同步对配置不完整");
            break;
        }

        QFileInfo rootInfo(m_pair.localPath);
        if (!rootInfo.exists() || !rootInfo.isDir()) {
            fatalError = tr("本地目录不存在或不是文件夹：%1").arg(m_pair.localPath);
            break;
        }
        if (!rootInfo.isReadable()) {
            fatalError = tr("本地目录不可读：%1").arg(m_pair.localPath);
            break;
        }

        SyncStateStore stateStore(m_stateDatabasePath);
        if (!stateStore.open(&settingsError)) {
            fatalError = settingsError;
            break;
        }
        m_stateStore = &stateStore;

        m_excludedLocal.clear();
        m_excludedRemote.clear();
        if (!scanLocal(&m_localSnapshot, &settingsError)) {
            if (m_cancelled.loadRelaxed())
                summary.cancelled = true;
            else
                fatalError = settingsError;
            break;
        }
        emitLog(tr("本地扫描完成：%1 个条目").arg(m_localSnapshot.size()));
        if (!m_pair.includePaths.isEmpty()) {
            for (auto it = m_localSnapshot.begin(); it != m_localSnapshot.end();) {
                if (includedInScope(it.key()))
                    ++it;
                else
                    it = m_localSnapshot.erase(it);
            }
            emitLog(tr("选择性同步：本地范围 %1 个条目").arg(m_localSnapshot.size()));
        }
        if (m_cancelled.loadRelaxed()) {
            summary.cancelled = true;
            break;
        }

        QHash<QString, StateEntry> state;
        if (!stateStore.load(m_pair.id, &state, &settingsError)) {
            fatalError = settingsError;
            break;
        }
        if (!m_pair.includePaths.isEmpty()) {
            for (auto it = state.begin(); it != state.end();) {
                if (includedInScope(it.key()))
                    ++it;
                else
                    it = state.erase(it);
            }
        }

        m_backend = createBackend(&fatalError);
        if (!m_backend) {
            if (fatalError.isEmpty())
                fatalError = tr("无法创建远程后端");
            break;
        }

        if (!m_backend->refreshAccessTokenIfNeeded(&settingsError)) {
            fatalError = settingsError;
            break;
        }

        RemoteEntry rootEntry;
        bool rootExists = false;
        if (!m_backend->stat(QString(), &rootEntry, &rootExists, &settingsError)) {
            fatalError = tr("检查远程同步根目录失败：%1").arg(settingsError);
            break;
        }
        if (!rootExists) {
            if (!state.isEmpty()) {
                fatalError = tr("远程同步根目录不存在，但此同步对已有历史状态。"
                                "为避免误删本地文件，同步已停止。请检查 WebDAV 地址，"
                                "或在确认后清空该同步对的基线状态。");
                break;
            }
            if (!m_backend->makeDirectory(QString(), &settingsError)) {
                fatalError = tr("创建远程同步根目录失败：%1").arg(settingsError);
                break;
            }
            emitLog(tr("已创建远程同步根目录"));
        }
        if (rootEntry.type != EntryType::Directory && rootExists) {
            fatalError = tr("远程同步根路径不是目录");
            break;
        }

        QList<RemoteEntry> remoteEntries;
        if (!m_backend->scan(&remoteEntries, &settingsError, [this]() {
                return m_cancelled.loadRelaxed() != 0;
            })) {
            if (m_cancelled.loadRelaxed())
                summary.cancelled = true;
            else
                fatalError = settingsError;
            break;
        }

        QMap<QString, RemoteEntry> remoteSnapshot;
        for (const RemoteEntry &entry : std::as_const(remoteEntries)) {
            if (PathUtils::isExcluded(entry.path, m_pair.excludePatterns)) {
                m_excludedRemote.insert(entry.path);
                continue;
            }
            bool shadowedLocally = false;
            for (const QString &excludedLocal : std::as_const(m_excludedLocal)) {
                if (PathUtils::isSameOrChild(entry.path, excludedLocal)) {
                    shadowedLocally = true;
                    break;
                }
            }
            if (shadowedLocally) {
                // A symlink or special local file occupies this path (or a
                // parent directory). Do not interpret the invisible local side
                // as a deletion and do not overwrite the symlink.
                m_excludedRemote.insert(entry.path);
                emitLog(tr("跳过被本地符号链接或排除项遮挡的远程条目"), entry.path);
                continue;
            }
            remoteSnapshot.insert(entry.path, entry);
        }
        if (!m_pair.includePaths.isEmpty()) {
            for (auto it = remoteSnapshot.begin(); it != remoteSnapshot.end();) {
                if (includedInScope(it.key()))
                    ++it;
                else
                    it = remoteSnapshot.erase(it);
            }
            emitLog(tr("选择性同步：远程范围 %1 个条目").arg(remoteSnapshot.size()));
        }
        m_remoteSnapshot = remoteSnapshot;
        if (m_pair.detectNameCollisions) {
            QString collisionError;
            if (!detectNameCollisions(&collisionError)) {
                fatalError = collisionError;
                break;
            }
        }
        emitLog(tr("远程扫描完成：%1 个条目").arg(remoteSnapshot.size()));
        if (m_cancelled.loadRelaxed()) {
            summary.cancelled = true;
            break;
        }

        plan = SyncPlanner::build(m_localSnapshot, remoteSnapshot, state, m_pair.syncDeletions);
        planBuilt = true;
        summary.conflictPaths = plan.conflicts;
        summary.conflictRecords.clear();
        for (const QString &path : std::as_const(plan.conflicts))
            summary.conflictRecords.append({path, {}});
        if (m_pair.syncMode != SyncMode::RemoteToLocal)
            detectLocalMoves(&plan, state);
        if (m_pair.syncMode != SyncMode::LocalToRemote)
            detectRemoteMoves(&plan, state);
        filterActionsForMode(&plan);
        emitLog(tr("同步计划：上传 %1，下载 %2，删除 %3，冲突 %4")
                    .arg(plan.uploads)
                    .arg(plan.downloads)
                    .arg(plan.deletes)
                    .arg(plan.conflicts.size()));

        for (const QString &path : std::as_const(plan.removedStates)) {
            if (!stateStore.removePath(m_pair.id, path, &settingsError)) {
                fatalError = settingsError;
                break;
            }
        }
        if (!fatalError.isEmpty())
            break;

        if (!plan.baselineUpdates.isEmpty()
            && !stateStore.upsertMany(m_pair.id, plan.baselineUpdates, &settingsError)) {
            fatalError = settingsError;
            break;
        }

        m_completedActions = 0;
        m_totalActions = plan.actions.size();
        m_bytesDone = 0;
        m_bytesTotal = 0;
        if (m_parallelTransfers > 1) {
            for (int index = 0; index < plan.actions.size();) {
                if (m_cancelled.loadRelaxed()) {
                    summary.cancelled = true;
                    break;
                }
                const SyncAction &action = plan.actions.at(index);
                if (action.kind == ActionKind::Upload || action.kind == ActionKind::Download) {
                    QList<TransferJob> jobs;
                    while (index < plan.actions.size()) {
                        const SyncAction &candidate = plan.actions.at(index);
                        if (candidate.kind != ActionKind::Upload
                            && candidate.kind != ActionKind::Download) {
                            break;
                        }
                        emit progress(m_completedActions, m_totalActions, m_bytesDone, m_bytesTotal,
                                      candidate.path);
                        TransferJob job;
                        QString error;
                        bool skipped = false;
                        if (!prepareTransferJob(candidate, index, &job, &skipped, &error)) {
                            addError(&summary, tr("“%1”：%2").arg(candidate.path, error));
                            if (!summary.failedPaths.contains(candidate.path))
                                summary.failedPaths.append(candidate.path);
                            ++m_completedActions;
                        } else if (skipped) {
                            ++summary.skipped;
                            ++m_completedActions;
                        } else {
                            jobs.append(job);
                        }
                        emit progress(m_completedActions, m_totalActions, m_bytesDone, m_bytesTotal,
                                      candidate.path);
                        ++index;
                    }
                    if (!jobs.isEmpty())
                        executeTransferBatch(jobs, &summary);
                    continue;
                }

                emit progress(m_completedActions, m_totalActions, m_bytesDone, m_bytesTotal,
                              action.path);
                if (!executeAction(action, &summary) && !summary.cancelled
                    && !summary.failedPaths.contains(action.path)) {
                    summary.failedPaths.append(action.path);
                }
                ++m_completedActions;
                emit progress(m_completedActions, m_totalActions, m_bytesDone, m_bytesTotal,
                              action.path);
                ++index;
            }
        } else {
            for (const SyncAction &action : std::as_const(plan.actions)) {
                if (m_cancelled.loadRelaxed()) {
                    summary.cancelled = true;
                    break;
                }
                emit progress(m_completedActions, m_totalActions, m_bytesDone, m_bytesTotal,
                              action.path);
                if (!executeAction(action, &summary) && !summary.cancelled
                    && !summary.failedPaths.contains(action.path)) {
                    summary.failedPaths.append(action.path);
                }
                ++m_completedActions;
                emit progress(m_completedActions, m_totalActions, m_bytesDone, m_bytesTotal,
                              action.path);
            }
        }

        if (m_cancelled.loadRelaxed())
            summary.cancelled = true;

        if (planBuilt && !summary.cancelled) {
            QString conflictError;
            if (!m_stateStore->replaceConflicts(m_pair.id, summary.conflictRecords,
                                                &conflictError)) {
                addError(&summary, tr("保存冲突记录失败：%1").arg(conflictError));
            }
        }
    } while (false);

    m_stateStore = nullptr;
    m_backend.reset();

    if (!fatalError.isEmpty()) {
        summary.success = false;
        summary.message = fatalError;
        addError(&summary, fatalError);
    } else if (summary.cancelled) {
        summary.success = false;
        summary.message = tr("同步已取消");
        emitLog(summary.message);
    } else {
        summary.success = summary.errors == 0;
        if (!summary.success) {
            summary.message = tr("同步完成，但有 %1 项错误").arg(summary.errors);
        } else if (plan.actions.isEmpty() && plan.baselineUpdates.isEmpty()) {
            summary.message = tr("已是最新状态");
        } else if (summary.conflicts > 0) {
            summary.message = tr("同步完成，处理了 %1 个冲突").arg(summary.conflicts);
        } else {
            summary.message = tr("同步完成");
        }
        emitLog(summary.message);
    }

    summary.elapsedMs = elapsed.elapsed();
    {
        const QMutexLocker locker(&m_summaryMutex);
        m_lastSummary = summary;
    }
    emit finished(summary);
}

bool SyncEngine::scanLocal(QMap<QString, LocalEntry> *entries, QString *error)
{
    if (!entries)
        return false;
    entries->clear();
    QFileInfo root(m_pair.localPath);
    if (!root.exists() || !root.isDir()) {
        if (error)
            *error = tr("本地目录不存在：%1").arg(m_pair.localPath);
        return false;
    }
    return scanLocalDirectory(QString(), entries, error);
}

bool SyncEngine::scanLocalDirectory(const QString &relativeDir, QMap<QString, LocalEntry> *entries,
                                    QString *error)
{
    if (m_cancelled.loadRelaxed()) {
        if (error)
            *error = tr("同步已取消");
        return false;
    }

    if (!relativeDir.isEmpty() && !includedInScope(relativeDir))
        return true;

    const QString absoluteDir = PathUtils::localFilePath(m_pair.localPath, relativeDir);
    QDir dir(absoluteDir);
    if (!dir.exists()) {
        if (error)
            *error = tr("本地目录不存在：%1").arg(absoluteDir);
        return false;
    }
    if (!dir.isReadable()) {
        if (error)
            *error = tr("无法读取本地目录：%1").arg(absoluteDir);
        return false;
    }

    const QFileInfoList entriesList =
        dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDir::DirsFirst | QDir::Name);

    for (const QFileInfo &info : entriesList) {
        if (m_cancelled.loadRelaxed()) {
            if (error)
                *error = tr("同步已取消");
            return false;
        }
        const QString relative = relativeDir.isEmpty()
            ? info.fileName()
            : relativeDir + QLatin1Char('/') + info.fileName();

        if (PathUtils::isExcluded(relative, m_pair.excludePatterns)) {
            m_excludedLocal.insert(relative);
            continue;
        }
        if (info.isSymLink()) {
            m_excludedLocal.insert(relative);
            emitLog(tr("跳过符号链接"), relative);
            continue;
        }
        if (info.isDir()) {
            LocalEntry entry;
            entry.path = relative;
            entry.type = EntryType::Directory;
            entry.size = 0;
            entry.mtimeMs = info.lastModified().toMSecsSinceEpoch();
            entries->insert(relative, entry);
            if (!scanLocalDirectory(relative, entries, error))
                return false;
            continue;
        }
        if (!info.isFile()) {
            m_excludedLocal.insert(relative);
            emitLog(tr("跳过特殊文件"), relative);
            continue;
        }
        LocalEntry entry;
        entry.path = relative;
        entry.type = EntryType::File;
        entry.size = info.size();
        entry.mtimeMs = info.lastModified().toMSecsSinceEpoch();
        entries->insert(relative, entry);
    }
    return true;
}

bool SyncEngine::statLocal(const QString &relativePath, LocalEntry *entry, bool *exists) const
{
    if (exists)
        *exists = false;
    const QString absolute = PathUtils::localFilePath(m_pair.localPath, relativePath);
    const QFileInfo info(absolute);
    if (!info.exists() && !info.isSymLink())
        return true;
    if (info.isSymLink()) {
        // Report the path as occupied so a concurrent symlink is never
        // interpreted as an absent file and overwritten or deleted.
        if (exists)
            *exists = true;
        if (entry) {
            entry->path = relativePath;
            entry->type = EntryType::File;
            entry->size = -1;
            entry->mtimeMs = 0;
        }
        return true;
    }
    if (!info.isFile() && !info.isDir()) {
        if (exists)
            *exists = true;
        if (entry) {
            entry->path = relativePath;
            entry->type = EntryType::File;
            entry->size = -1;
            entry->mtimeMs = 0;
        }
        return true;
    }
    if (exists)
        *exists = true;
    if (entry) {
        entry->path = relativePath;
        entry->type = info.isDir() ? EntryType::Directory : EntryType::File;
        entry->size = info.isFile() ? info.size() : 0;
        entry->mtimeMs = info.lastModified().toMSecsSinceEpoch();
    }
    return true;
}

bool SyncEngine::ensureLocalParent(const QString &relativePath, QString *error) const
{
    const QString parent = PathUtils::parentPath(relativePath);
    if (parent.isEmpty())
        return true;
    const QString abs = PathUtils::localFilePath(m_pair.localPath, parent);
    if (!QDir().mkpath(abs)) {
        if (error)
            *error = tr("无法创建本地目录：%1").arg(abs);
        return false;
    }
    return true;
}

bool SyncEngine::localSubtreeStillMatchesSnapshot(const QString &prefix) const
{
    const QString absolute = PathUtils::localFilePath(m_pair.localPath, prefix);
    QFileInfo prefixInfo(absolute);
    if (!prefixInfo.exists() || !prefixInfo.isDir())
        return false;

    for (const QString &excluded : m_excludedLocal) {
        if (PathUtils::isSameOrChild(excluded, prefix))
            return false;
    }

    QSet<QString> snapshotPaths;
    for (auto it = m_localSnapshot.cbegin(); it != m_localSnapshot.cend(); ++it) {
        if (PathUtils::isSameOrChild(it.key(), prefix))
            snapshotPaths.insert(it.key());
    }

    QDirIterator iterator(absolute,
                          QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        const QFileInfo info = iterator.fileInfo();
        if (info.isSymLink())
            return false;
        const QString relative = QDir(m_pair.localPath).relativeFilePath(path);
        if (!relative.startsWith(prefix + QLatin1Char('/')) && relative != prefix)
            continue;
        if (PathUtils::isExcluded(relative, m_pair.excludePatterns))
            return false;
        if (!snapshotPaths.contains(relative))
            return false;
        const LocalEntry &snapshot = m_localSnapshot.value(relative);
        if (info.isDir()) {
            if (snapshot.type != EntryType::Directory)
                return false;
        } else if (info.isFile()) {
            if (snapshot.type != EntryType::File || snapshot.size != info.size()
                || snapshot.mtimeMs != info.lastModified().toMSecsSinceEpoch()) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

bool SyncEngine::localDeleteStillSafe(const SyncAction &action) const
{
    if (!action.local.isValid())
        return false;
    LocalEntry current;
    bool exists = false;
    statLocal(action.path, &current, &exists);
    if (!exists)
        return true;
    if (current.type != action.local.type)
        return false;
    if (current.type == EntryType::File) {
        return current.size == action.local.size && current.mtimeMs == action.local.mtimeMs;
    }
    return localSubtreeStillMatchesSnapshot(action.path);
}

bool SyncEngine::verifyRemoteUnchanged(const SyncAction &action, QString *error,
                                       QString *currentEtag)
{
    if (currentEtag)
        currentEtag->clear();
    RemoteEntry current;
    bool exists = false;
    QString statError;
    if (!m_backend->stat(action.path, &current, &exists, &statError)) {
        if (error)
            *error = tr("检查远程状态失败：%1").arg(statError);
        return false;
    }

    if (!action.remote.isValid()) {
        if (exists) {
            if (error)
                *error = tr("远程路径在同步期间被其他客户端创建");
            return false;
        }
        return true;
    }

    if (!exists) {
        if (error)
            *error = tr("远程路径在同步期间被其他客户端删除");
        return false;
    }
    if (!sameRemoteMetadata(current, action.remote)) {
        if (error)
            *error = tr("远程内容在同步期间发生了变化，本次操作已取消");
        return false;
    }
    if (currentEtag)
        *currentEtag = PathUtils::normalizeEtag(current.etag);
    return true;
}

bool SyncEngine::recordSynced(const QString &path, QString *error)
{
    if (!m_stateStore)
        return false;

    LocalEntry local;
    bool localExists = false;
    statLocal(path, &local, &localExists);
    RemoteEntry remote;
    bool remoteExists = false;
    QString backendError;
    if (!m_backend->stat(path, &remote, &remoteExists, &backendError)) {
        if (error)
            *error = tr("上传/下载成功，但读取远程状态失败：%1").arg(backendError);
        return false;
    }
    if (!localExists || !remoteExists) {
        if (error)
            *error = tr("操作成功，但任一侧的条目已不存在，无法记录同步状态");
        return false;
    }
    if (local.type != remote.type) {
        if (error)
            *error = tr("操作后本地与远程类型不一致，无法记录同步状态");
        return false;
    }

    StateEntry state = local.type == EntryType::Directory ? SyncPlanner::makeDirectoryState(path)
                                                          : SyncPlanner::makeState(local, remote);
    QString stateError;
    if (!m_stateStore->upsert(m_pair.id, state, &stateError)) {
        if (error)
            *error = tr("操作成功，但写入同步状态失败：%1").arg(stateError);
        return false;
    }
    return true;
}

bool SyncEngine::doUpload(const SyncAction &action, const QString &localFile,
                          const QString &remotePath, const QString &expectedEtag, QString *error)
{
    if (!m_backend->upload(localFile, remotePath, expectedEtag, error, [this]() {
            return m_cancelled.loadRelaxed() != 0;
        })) {
        return false;
    }
    if (!recordSynced(remotePath, error))
        return false;
    Q_UNUSED(action)
    return true;
}

bool SyncEngine::doDownload(const SyncAction &action, const QString &remotePath,
                            const QString &localFile, QString *error)
{
    DownloadResult result;
    if (!m_backend->download(remotePath, localFile, &result, error, [this]() {
            return m_cancelled.loadRelaxed() != 0;
        })) {
        return false;
    }
    if (!recordSynced(remotePath, error))
        return false;
    Q_UNUSED(action)
    return true;
}

bool SyncEngine::detectNameCollisions(QString *error) const
{
    QHash<QString, QSet<QString>> groups;
    auto add = [&groups](const QString &path) {
        groups[PathUtils::normalizedCollisionKey(path)].insert(path);
    };
    for (auto it = m_localSnapshot.constBegin(); it != m_localSnapshot.constEnd(); ++it)
        add(it.key());
    for (auto it = m_remoteSnapshot.constBegin(); it != m_remoteSnapshot.constEnd(); ++it)
        add(it.key());

    QStringList collisions;
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        if (it.value().size() <= 1)
            continue;
        QStringList paths = it.value().values();
        paths.sort();
        collisions.append(paths.join(QStringLiteral("  ⇄  ")));
    }
    if (collisions.isEmpty())
        return true;
    collisions.sort();
    if (error) {
        *error = tr("检测到大小写或 Unicode 规范化名称冲突，已停止同步以避免覆盖：\n%1\n"
                    "请在“编辑同步对”中重命名冲突项，或关闭名称冲突检测。")
                     .arg(collisions.join(QLatin1Char('\n')));
    }
    return false;
}

void SyncEngine::detectLocalMoves(SyncPlan *plan, const QHash<QString, StateEntry> &state) const
{
    QHash<QString, SyncAction> deletes;
    QHash<QString, SyncAction> uploads;
    for (const SyncAction &action : std::as_const(plan->actions)) {
        if (action.kind == ActionKind::DeleteRemote && action.remote.type == EntryType::File
            && !action.local.isValid()) {
            deletes.insert(action.path, action);
        } else if (action.kind == ActionKind::Upload && action.local.type == EntryType::File
                   && !action.remote.isValid()) {
            uploads.insert(action.path, action);
        }
    }
    QSet<QString> consumedDeletes;
    QSet<QString> consumedUploads;
    QList<SyncAction> moves;
    for (auto deleteIt = deletes.constBegin(); deleteIt != deletes.constEnd(); ++deleteIt) {
        const StateEntry previous = state.value(deleteIt.key());
        if (previous.type != EntryType::File || previous.localSize <= 0)
            continue;
        for (auto uploadIt = uploads.constBegin(); uploadIt != uploads.constEnd(); ++uploadIt) {
            if (consumedUploads.contains(uploadIt.key()))
                continue;
            const LocalEntry &candidate = uploadIt.value().local;
            if (candidate.size != previous.localSize || candidate.mtimeMs != previous.localMtimeMs)
                continue;
            SyncAction move;
            move.kind = ActionKind::MoveRemote;
            move.path = uploadIt.key();
            move.fromPath = deleteIt.key();
            move.local = candidate;
            move.remote = deleteIt.value().remote;
            move.state = previous;
            move.stateExists = true;
            moves.append(move);
            consumedDeletes.insert(deleteIt.key());
            consumedUploads.insert(uploadIt.key());
            break;
        }
    }
    if (moves.isEmpty())
        return;

    QList<SyncAction> ordered;
    int movePosition = -1;
    for (const SyncAction &action : std::as_const(plan->actions)) {
        if (consumedDeletes.contains(action.path) && action.kind == ActionKind::DeleteRemote)
            continue;
        if (consumedUploads.contains(action.path) && action.kind == ActionKind::Upload)
            continue;
        if (movePosition < 0
            && (action.kind == ActionKind::DeleteLocal
                || action.kind == ActionKind::DeleteRemote)) {
            movePosition = ordered.size();
        }
        ordered.append(action);
    }
    if (movePosition < 0)
        movePosition = ordered.size();
    for (int index = 0; index < moves.size(); ++index)
        ordered.insert(movePosition + index, moves.at(index));
    plan->actions = ordered;
}

void SyncEngine::detectRemoteMoves(SyncPlan *plan, const QHash<QString, StateEntry> &state) const
{
    QHash<QString, SyncAction> deletes;
    QHash<QString, SyncAction> downloads;
    for (const SyncAction &action : std::as_const(plan->actions)) {
        if (action.kind == ActionKind::DeleteLocal && action.local.type == EntryType::File
            && !action.remote.isValid()) {
            deletes.insert(action.path, action);
        } else if (action.kind == ActionKind::Download && action.remote.type == EntryType::File
                   && !action.local.isValid()) {
            downloads.insert(action.path, action);
        }
    }
    QSet<QString> consumedDeletes;
    QSet<QString> consumedDownloads;
    QList<SyncAction> moves;
    for (auto deleteIt = deletes.constBegin(); deleteIt != deletes.constEnd(); ++deleteIt) {
        const StateEntry previous = state.value(deleteIt.key());
        if (previous.type != EntryType::File || previous.remoteEtag.isEmpty())
            continue;
        for (auto downloadIt = downloads.constBegin(); downloadIt != downloads.constEnd();
             ++downloadIt) {
            if (consumedDownloads.contains(downloadIt.key()))
                continue;
            const RemoteEntry &candidate = downloadIt.value().remote;
            if (candidate.size != previous.remoteSize
                || PathUtils::normalizeEtag(candidate.etag) != previous.remoteEtag) {
                continue;
            }
            SyncAction move;
            move.kind = ActionKind::MoveLocal;
            move.path = downloadIt.key();
            move.fromPath = deleteIt.key();
            move.local = deleteIt.value().local;
            move.remote = candidate;
            move.state = previous;
            move.stateExists = true;
            moves.append(move);
            consumedDeletes.insert(deleteIt.key());
            consumedDownloads.insert(downloadIt.key());
            break;
        }
    }
    if (moves.isEmpty())
        return;

    QList<SyncAction> ordered;
    int movePosition = -1;
    for (const SyncAction &action : std::as_const(plan->actions)) {
        if (consumedDeletes.contains(action.path) && action.kind == ActionKind::DeleteLocal)
            continue;
        if (consumedDownloads.contains(action.path) && action.kind == ActionKind::Download)
            continue;
        if (movePosition < 0 && action.kind == ActionKind::DeleteLocal)
            movePosition = ordered.size();
        ordered.append(action);
    }
    if (movePosition < 0)
        movePosition = ordered.size();
    for (int index = 0; index < moves.size(); ++index)
        ordered.insert(movePosition + index, moves.at(index));
    plan->actions = ordered;
}

bool SyncEngine::moveLocal(const SyncAction &action, QString *error)
{
    const QString source = PathUtils::localFilePath(m_pair.localPath, action.fromPath);
    const QString destination = PathUtils::localFilePath(m_pair.localPath, action.path);
    QFileInfo sourceInfo(source);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        if (error)
            *error = tr("本地重命名源不存在：%1").arg(source);
        return false;
    }
    if (QFileInfo::exists(destination)) {
        if (error)
            *error = tr("本地重命名目标已存在：%1").arg(destination);
        return false;
    }
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
        if (error)
            *error = tr("无法创建本地重命名目标目录");
        return false;
    }
    if (!QFile::rename(source, destination)) {
        if (error)
            *error = tr("本地重命名失败：%1").arg(action.path);
        return false;
    }
    if (m_stateStore && !m_stateStore->removePath(m_pair.id, action.fromPath, error))
        return false;
    return recordSynced(action.path, error);
}

bool SyncEngine::moveRemote(const SyncAction &action, QString *error)
{
    SyncAction previous;
    previous.path = action.fromPath;
    previous.remote = action.remote;
    previous.local = {};
    QString verifyError;
    if (!verifyRemoteUnchanged(previous, &verifyError)) {
        if (error)
            *error = verifyError;
        return false;
    }
    if (!m_backend->move(action.fromPath, action.path, true, error, [this]() {
            return m_cancelled.loadRelaxed() != 0;
        })) {
        return false;
    }
    if (m_stateStore && !m_stateStore->removePath(m_pair.id, action.fromPath, error))
        return false;
    return recordSynced(action.path, error);
}

void SyncEngine::filterActionsForMode(SyncPlan *plan) const
{
    if (m_pair.syncMode == SyncMode::Bidirectional)
        return;
    QList<SyncAction> filtered;
    for (const SyncAction &action : std::as_const(plan->actions)) {
        const bool localToRemote = m_pair.syncMode == SyncMode::LocalToRemote;
        switch (action.kind) {
        case ActionKind::Upload:
        case ActionKind::CreateRemoteDir:
        case ActionKind::DeleteRemote:
            if (localToRemote)
                filtered.append(action);
            break;
        case ActionKind::Download:
        case ActionKind::CreateLocalDir:
        case ActionKind::DeleteLocal:
            if (!localToRemote)
                filtered.append(action);
            break;
        case ActionKind::ResolveConflict:
            filtered.append(action);
            break;
        case ActionKind::MoveRemote:
            if (localToRemote)
                filtered.append(action);
            break;
        case ActionKind::MoveLocal:
            if (!localToRemote)
                filtered.append(action);
            break;
        }
    }
    plan->actions = filtered;
    plan->uploads = 0;
    plan->downloads = 0;
    plan->deletes = 0;
    for (const SyncAction &action : std::as_const(plan->actions)) {
        if (action.kind == ActionKind::Upload)
            ++plan->uploads;
        else if (action.kind == ActionKind::Download)
            ++plan->downloads;
        else if (action.kind == ActionKind::DeleteLocal || action.kind == ActionKind::DeleteRemote)
            ++plan->deletes;
    }
}

bool SyncEngine::includedInScope(const QString &path) const
{
    if (m_pair.includePaths.isEmpty())
        return true;
    for (const QString &include : m_pair.includePaths) {
        if (PathUtils::isSameOrChild(path, include) || PathUtils::isSameOrChild(include, path)) {
            return true;
        }
    }
    return false;
}

bool SyncEngine::ensureTrashBatch(QString *error)
{
    if (!m_pair.trashDeletedFiles)
        return true;
    if (!m_trashBatchPath.isEmpty())
        return true;
    m_trashBatchPath = TrashManager::createBatch(m_pair.id, m_pair.trashRetentionDays, error);
    return !m_trashBatchPath.isEmpty();
}

bool SyncEngine::deleteRemote(const SyncAction &action, QString *error)
{
    RemoteEntry current;
    bool exists = false;
    QString statError;
    if (!m_backend->stat(action.path, &current, &exists, &statError)) {
        if (error)
            *error = tr("检查远程状态失败：%1").arg(statError);
        return false;
    }
    if (!exists) {
        if (m_stateStore) {
            QString stateError;
            if (!m_stateStore->removePrefix(m_pair.id, action.path, &stateError)) {
                if (error)
                    *error = stateError;
                return false;
            }
        }
        return true;
    }
    if (current.type != action.remote.type) {
        if (error)
            *error = tr("远程条目类型在同步期间发生了变化");
        return false;
    }
    if (!action.remote.etag.isEmpty() && !current.etag.isEmpty()
        && PathUtils::normalizeEtag(action.remote.etag) != PathUtils::normalizeEtag(current.etag)) {
        if (error)
            *error = tr("远程内容在同步期间发生了变化，未执行删除");
        return false;
    }
    if (current.type == EntryType::File && action.remote.etag.isEmpty()
        && (current.size != action.remote.size
            || (action.remote.mtimeMs > 0 && current.mtimeMs > 0
                && std::llabs(current.mtimeMs - action.remote.mtimeMs)
                    > kRemoteMtimeToleranceMs))) {
        if (error)
            *error = tr("远程文件在同步期间发生了变化，未执行删除");
        return false;
    }

    if (m_pair.trashDeletedFiles) {
        QString trashError;
        if (!ensureTrashBatch(&trashError)) {
            if (error)
                *error = trashError;
            return false;
        }
        if (current.type == EntryType::File) {
            if (!TrashManager::downloadRemoteFile(
                    *m_backend, action.path, m_trashBatchPath, action.path,
                    [this]() {
                        return m_cancelled.loadRelaxed() != 0;
                    },
                    &trashError)) {
                if (error)
                    *error = tr("备份远程文件到回收站失败：%1").arg(trashError);
                return false;
            }
        } else {
            for (auto it = m_remoteSnapshot.constBegin(); it != m_remoteSnapshot.constEnd(); ++it) {
                if (!PathUtils::isSameOrChild(it.key(), action.path))
                    continue;
                const QString destination = QDir(m_trashBatchPath).filePath(it.key());
                if (it.value().type == EntryType::Directory) {
                    if (!QDir().mkpath(destination)) {
                        if (error)
                            *error = tr("无法创建回收站目录“%1”").arg(destination);
                        return false;
                    }
                } else if (!TrashManager::downloadRemoteFile(
                               *m_backend, it.key(), m_trashBatchPath, it.key(),
                               [this]() {
                                   return m_cancelled.loadRelaxed() != 0;
                               },
                               &trashError)) {
                    if (error)
                        *error = tr("备份远程文件到回收站失败：%1").arg(trashError);
                    return false;
                }
            }
        }
    }

    if (!m_backend->remove(action.path, current.type == EntryType::Directory,
                           PathUtils::normalizeEtag(current.etag), error, [this]() {
                               return m_cancelled.loadRelaxed() != 0;
                           })) {
        return false;
    }
    if (m_stateStore) {
        QString stateError;
        if (!m_stateStore->removePrefix(m_pair.id, action.path, &stateError)) {
            if (error)
                *error = tr("删除成功，但移除同步状态失败：%1").arg(stateError);
            return false;
        }
    }
    return true;
}

bool SyncEngine::deleteLocal(const SyncAction &action, QString *error)
{
    const QString absolute = PathUtils::localFilePath(m_pair.localPath, action.path);
    QFileInfo info(absolute);
    if (!info.exists()) {
        if (m_stateStore) {
            QString stateError;
            if (!m_stateStore->removePrefix(m_pair.id, action.path, &stateError)) {
                if (error)
                    *error = stateError;
                return false;
            }
        }
        return true;
    }
    if (!localDeleteStillSafe(action)) {
        if (error)
            *error = tr("本地文件在同步期间发生变化，未执行删除");
        return false;
    }

    if (m_pair.trashDeletedFiles) {
        QString trashError;
        if (!ensureTrashBatch(&trashError)) {
            if (error)
                *error = trashError;
            return false;
        }
        if (!TrashManager::copyLocalPath(absolute, m_trashBatchPath, action.path, &trashError)) {
            if (error)
                *error = tr("备份本地文件到回收站失败：%1").arg(trashError);
            return false;
        }
    }

    bool removed = false;
    if (action.local.type == EntryType::Directory) {
        removed = QDir(absolute).removeRecursively();
    } else {
        removed = QFile::remove(absolute);
    }
    if (!removed && QFileInfo::exists(absolute)) {
        if (error)
            *error = tr("无法删除本地路径“%1”").arg(absolute);
        return false;
    }

    if (m_stateStore) {
        QString stateError;
        if (!m_stateStore->removePrefix(m_pair.id, action.path, &stateError)) {
            if (error)
                *error = tr("删除成功，但移除同步状态失败：%1").arg(stateError);
            return false;
        }
    }
    return true;
}

bool SyncEngine::contentsEqual(const QString &localPath, const RemoteEntry &remote,
                               QString *temporaryRemoteFile, QString *error)
{
    if (temporaryRemoteFile)
        temporaryRemoteFile->clear();
    if (remote.size != QFileInfo(localPath).size())
        return false;

    const QFileInfo localBefore(localPath);
    const QString temp = uniqueTemporaryPath();
    DownloadResult result;
    QString downloadError;
    if (!m_backend->download(remote.path, temp, &result, &downloadError, [this]() {
            return m_cancelled.loadRelaxed() != 0;
        })) {
        QFile::remove(temp);
        if (error)
            *error = tr("下载远程副本用于比较失败：%1").arg(downloadError);
        return false;
    }

    const QFileInfo localAfter(localPath);
    if (!localAfter.exists() || localAfter.size() != localBefore.size()
        || localAfter.lastModified() != localBefore.lastModified()) {
        QFile::remove(temp);
        if (error)
            *error = tr("本地文件在比较期间发生了变化，请重试");
        return false;
    }

    QString hashError;
    const QString localHash = hashFile(localPath, &hashError);
    if (localHash.isEmpty()) {
        QFile::remove(temp);
        if (error)
            *error = hashError;
        return false;
    }
    const QString remoteHash = hashFile(temp, &hashError);
    if (remoteHash.isEmpty()) {
        QFile::remove(temp);
        if (error)
            *error = hashError;
        return false;
    }

    if (temporaryRemoteFile)
        *temporaryRemoteFile = temp;
    else
        QFile::remove(temp);
    return localHash == remoteHash;
}

bool SyncEngine::chooseConflictPath(const QString &path, QString *conflictPath, QString *error)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (int attempt = 0; attempt < 100; ++attempt) {
        const QString candidate = PathUtils::conflictRelativePath(path, now, attempt);
        const QString absolute = PathUtils::localFilePath(m_pair.localPath, candidate);
        if (QFileInfo::exists(absolute))
            continue;
        RemoteEntry remote;
        bool exists = false;
        QString statError;
        if (!m_backend->stat(candidate, &remote, &exists, &statError)) {
            if (error)
                *error = tr("检查冲突副本路径失败：%1").arg(statError);
            return false;
        }
        if (exists)
            continue;
        if (conflictPath)
            *conflictPath = candidate;
        return true;
    }
    if (error)
        *error = tr("无法生成唯一的冲突副本文件名");
    return false;
}

bool SyncEngine::resolveKeepBoth(const SyncAction &action, const QString &temporaryRemoteFile,
                                 SyncSummary *summary)
{
    QString conflictPath;
    QString error;
    if (!chooseConflictPath(action.path, &conflictPath, &error)) {
        addError(summary, tr("“%1”：%2").arg(action.path, error));
        return false;
    }
    if (!ensureLocalParent(conflictPath, &error)) {
        addError(summary, tr("“%1”：%2").arg(action.path, error));
        return false;
    }

    const QString conflictAbsolute = PathUtils::localFilePath(m_pair.localPath, conflictPath);
    bool haveLocalCopy = false;
    if (!temporaryRemoteFile.isEmpty() && QFileInfo::exists(temporaryRemoteFile)) {
        const QDateTime mtime = QFileInfo(temporaryRemoteFile).lastModified();
        haveLocalCopy = QFile::copy(temporaryRemoteFile, conflictAbsolute);
        if (haveLocalCopy && mtime.isValid()) {
            QFile copy(conflictAbsolute);
            if (copy.open(QIODevice::ReadWrite))
                copy.setFileTime(mtime, QFileDevice::FileModificationTime);
        }
        QFile::remove(temporaryRemoteFile);
    }
    if (!haveLocalCopy) {
        DownloadResult downloaded;
        QString downloadError;
        if (!m_backend->download(action.remote.path, conflictAbsolute, &downloaded, &downloadError,
                                 [this]() {
                                     return m_cancelled.loadRelaxed() != 0;
                                 })) {
            addError(summary, tr("“%1”：下载冲突副本失败：%2").arg(action.path, downloadError));
            return false;
        }
    }

    summary->conflictCopies.append(conflictPath);
    emitLog(tr("保留冲突副本"), conflictPath);

    QString uploadError;
    if (!m_backend->upload(conflictAbsolute, conflictPath, QStringLiteral("*"), &uploadError,
                           [this]() {
                               return m_cancelled.loadRelaxed() != 0;
                           })) {
        addError(summary,
                 tr("“%1”：冲突副本已保存在本地，但上传失败：%2").arg(conflictPath, uploadError));
        return false;
    }
    if (!recordSynced(conflictPath, &uploadError)) {
        addError(summary, tr("“%1”：%2").arg(conflictPath, uploadError));
        return false;
    }

    const QString absolute = PathUtils::localFilePath(m_pair.localPath, action.path);
    QString etag;
    if (!verifyRemoteUnchanged(action, &uploadError, &etag)) {
        addError(summary, tr("“%1”：%2").arg(action.path, uploadError));
        return false;
    }
    if (!doUpload(action, absolute, action.path, etag, &uploadError)) {
        addError(summary,
                 tr("“%1”：冲突副本已保留，但本地版本上传失败：%2").arg(action.path, uploadError));
        return false;
    }
    return true;
}

bool SyncEngine::resolveConflict(const SyncAction &action, SyncSummary *summary)
{
    if (m_cancelled.loadRelaxed())
        return false;

    const bool hasLocal = action.local.isValid();
    const bool hasRemote = action.remote.isValid();
    QString error;

    if (hasLocal && hasRemote && action.local.type != action.remote.type) {
        ++summary->conflicts;
        addError(summary,
                 tr("“%1”：无法自动解决文件与目录类型冲突，请在两侧手动处理").arg(action.path));
        return false;
    }

    auto currentLocalStillMatches = [&]() {
        LocalEntry current;
        bool exists = false;
        statLocal(action.path, &current, &exists);
        if (!exists || current.type != action.local.type)
            return false;
        if (current.type == EntryType::File) {
            return current.size == action.local.size && current.mtimeMs == action.local.mtimeMs;
        }
        return true;
    };

    auto uploadLocal = [&]() -> bool {
        if (!currentLocalStillMatches()) {
            addError(summary, tr("“%1”：本地文件在同步期间发生变化，请重试").arg(action.path));
            return false;
        }
        QString etag;
        if (!verifyRemoteUnchanged(action, &error, &etag)) {
            addError(summary, tr("“%1”：%2").arg(action.path, error));
            return false;
        }
        const QString absolute = PathUtils::localFilePath(m_pair.localPath, action.path);
        const QString expected = action.remote.isValid() ? etag : QStringLiteral("*");
        if (!doUpload(action, absolute, action.path, expected, &error)) {
            addError(summary, tr("“%1”：%2").arg(action.path, error));
            return false;
        }
        ++summary->uploaded;
        return true;
    };

    auto downloadRemote = [&]() -> bool {
        if (action.local.isValid() && !currentLocalStillMatches()) {
            addError(summary, tr("“%1”：本地文件在同步期间发生变化，请重试").arg(action.path));
            return false;
        }
        if (!action.local.isValid()) {
            LocalEntry current;
            bool exists = false;
            statLocal(action.path, &current, &exists);
            if (exists) {
                addError(summary, tr("“%1”：本地路径在同步期间出现，请重试").arg(action.path));
                return false;
            }
        }
        if (!ensureLocalParent(action.path, &error)) {
            addError(summary, tr("“%1”：%2").arg(action.path, error));
            return false;
        }
        const QString absolute = PathUtils::localFilePath(m_pair.localPath, action.path);
        if (!doDownload(action, action.path, absolute, &error)) {
            addError(summary, tr("“%1”：%2").arg(action.path, error));
            return false;
        }
        ++summary->downloaded;
        return true;
    };

    if (hasLocal && !hasRemote) {
        // Remote deletion vs. local modification.
        ++summary->conflicts;
        if (m_pair.conflictPolicy == ConflictPolicy::PreferRemote) {
            if (!deleteLocal(action, &error)) {
                addError(summary, tr("“%1”：%2").arg(action.path, error));
                return false;
            }
            ++summary->deletedLocal;
            emitLog(tr("远程删除优先，已删除本地修改"), action.path);
            return true;
        }
        emitLog(tr("远程已删除但本地有修改，重新上传本地版本"), action.path);
        return uploadLocal();
    }

    if (!hasLocal && hasRemote) {
        // Local deletion vs. remote modification.
        ++summary->conflicts;
        if (!m_pair.syncDeletions) {
            emitLog(tr("同步删除已关闭：本地删除不传播，恢复远程版本"), action.path);
            return downloadRemote();
        }
        if (m_pair.conflictPolicy == ConflictPolicy::PreferLocal) {
            if (!deleteRemote(action, &error)) {
                addError(summary, tr("“%1”：%2").arg(action.path, error));
                return false;
            }
            ++summary->deletedRemote;
            emitLog(tr("本地删除优先，已删除远程修改"), action.path);
            return true;
        }
        emitLog(tr("本地已删除但远程有修改，恢复远程版本"), action.path);
        return downloadRemote();
    }

    // Both sides still exist and are files.
    if (!verifyRemoteUnchanged(action, &error)) {
        addError(summary, tr("“%1”：%2").arg(action.path, error));
        return false;
    }
    ConflictPolicy policy = m_pair.conflictPolicy;
    if (policy == ConflictPolicy::NewerWins) {
        ++summary->conflicts;
        if (action.remote.mtimeMs > action.local.mtimeMs) {
            emitLog(tr("冲突：远程版本较新，下载"), action.path);
            return downloadRemote();
        }
        emitLog(tr("冲突：本地版本较新，上传"), action.path);
        return uploadLocal();
    }
    if (policy == ConflictPolicy::PreferRemote) {
        ++summary->conflicts;
        emitLog(tr("冲突：按策略使用远程版本"), action.path);
        return downloadRemote();
    }
    if (policy == ConflictPolicy::PreferLocal) {
        ++summary->conflicts;
        emitLog(tr("冲突：按策略使用本地版本"), action.path);
        return uploadLocal();
    }

    const QString absolute = PathUtils::localFilePath(m_pair.localPath, action.path);
    if (!currentLocalStillMatches()) {
        addError(summary, tr("“%1”：本地文件在同步期间发生变化，请重试").arg(action.path));
        return false;
    }
    QString temporaryRemoteFile;
    if (!contentsEqual(absolute, action.remote, &temporaryRemoteFile, &error)) {
        if (!error.isEmpty()) {
            addError(summary, tr("“%1”：%2").arg(action.path, error));
            return false;
        }
        ++summary->conflicts;
        return resolveKeepBoth(action, temporaryRemoteFile, summary);
    }
    QFile::remove(temporaryRemoteFile);
    if (!recordSynced(action.path, &error)) {
        addError(summary, tr("“%1”：%2").arg(action.path, error));
        return false;
    }
    emitLog(tr("冲突：两端内容相同，仅更新同步状态"), action.path);
    return true;
}

bool SyncEngine::executeAction(const SyncAction &action, SyncSummary *summary)
{
    if (m_cancelled.loadRelaxed())
        return false;

    auto failed = [&](const QString &error) {
        if (m_cancelled.loadRelaxed())
            return false;
        addError(summary, tr("“%1”：%2").arg(action.path, error));
        return false;
    };

    auto verifyLocalSnapshot = [&]() -> bool {
        LocalEntry current;
        bool exists = false;
        statLocal(action.path, &current, &exists);
        if (!action.local.isValid())
            return !exists;
        if (!exists || current.type != action.local.type)
            return false;
        if (current.type == EntryType::File) {
            return current.size == action.local.size && current.mtimeMs == action.local.mtimeMs;
        }
        return true;
    };

    if (action.path.isEmpty() && action.kind != ActionKind::CreateRemoteDir
        && action.kind != ActionKind::CreateLocalDir) {
        return failed(tr("内部错误：拒绝操作同步根目录"));
    }

    switch (action.kind) {
    case ActionKind::Upload: {
        if (!verifyLocalSnapshot())
            return failed(tr("本地文件在开始同步后发生变化，将在下次同步处理"));
        QString etag;
        QString error;
        if (!verifyRemoteUnchanged(action, &error, &etag))
            return failed(error);
        const QString absolute = PathUtils::localFilePath(m_pair.localPath, action.path);
        const QString expected = action.remote.isValid() ? etag : QStringLiteral("*");

        // A file can change mtime without changing content (for example, a
        // password manager touching its database). Do not overwrite the remote
        // copy in that case; refresh the baseline instead.
        if (action.remote.isValid()) {
            QString compareError;
            const bool equal = contentsEqual(absolute, action.remote, nullptr, &compareError);
            if (!compareError.isEmpty()) {
                emitLog(tr("检查远端内容失败，继续上传：%1").arg(compareError), action.path);
            } else if (equal) {
                if (!recordSynced(action.path, &error))
                    return failed(error);
                ++summary->skipped;
                emitLog(tr("内容与远端一致，跳过上传"), action.path);
                return true;
            }
        }
        if (!verifyLocalSnapshot())
            return failed(tr("本地文件在内容检查期间发生变化，将在下次同步处理"));

        emitLog(tr("上传"), action.path);
        if (!doUpload(action, absolute, action.path, expected, &error))
            return failed(error);
        ++summary->uploaded;
        return true;
    }
    case ActionKind::Download: {
        if (!verifyLocalSnapshot())
            return failed(tr("本地路径在开始同步后发生变化，将在下次同步处理"));
        QString error;
        if (!ensureLocalParent(action.path, &error))
            return failed(error);
        emitLog(tr("下载"), action.path);
        const QString absolute = PathUtils::localFilePath(m_pair.localPath, action.path);
        if (!doDownload(action, action.path, absolute, &error))
            return failed(error);
        ++summary->downloaded;
        return true;
    }
    case ActionKind::CreateRemoteDir: {
        QString error;
        emitLog(tr("创建远程目录"), action.path);
        if (!m_backend->makeDirectory(action.path, &error))
            return failed(error);
        if (!recordSynced(action.path, &error))
            return failed(error);
        ++summary->directoriesCreated;
        return true;
    }
    case ActionKind::CreateLocalDir: {
        QString error;
        const QString absolute = PathUtils::localFilePath(m_pair.localPath, action.path);
        QFileInfo info(absolute);
        if (info.exists() && !info.isDir())
            return failed(tr("本地已存在同名文件"));
        if (!QDir().mkpath(absolute))
            return failed(tr("无法创建本地目录"));
        emitLog(tr("创建本地目录"), action.path);
        if (!recordSynced(action.path, &error))
            return failed(error);
        ++summary->directoriesCreated;
        return true;
    }
    case ActionKind::DeleteRemote: {
        if (action.remote.type == EntryType::Directory) {
            for (const QString &excluded : m_excludedRemote) {
                if (PathUtils::isSameOrChild(excluded, action.path)) {
                    ++summary->skipped;
                    emitLog(tr("远程目录包含排除项，已跳过目录删除"), action.path);
                    return true;
                }
            }
        }
        QString error;
        emitLog(tr("删除远程"), action.path);
        if (!deleteRemote(action, &error))
            return failed(error);
        ++summary->deletedRemote;
        return true;
    }
    case ActionKind::DeleteLocal: {
        if (action.local.type == EntryType::Directory) {
            for (const QString &excluded : m_excludedLocal) {
                if (PathUtils::isSameOrChild(excluded, action.path)) {
                    ++summary->skipped;
                    emitLog(tr("本地目录包含排除项，已跳过目录删除"), action.path);
                    return true;
                }
            }
        }
        QString error;
        emitLog(tr("删除本地"), action.path);
        if (!deleteLocal(action, &error))
            return failed(error);
        ++summary->deletedLocal;
        return true;
    }
    case ActionKind::MoveRemote: {
        QString error;
        emitLog(tr("移动远程文件"), action.fromPath + QStringLiteral(" → ") + action.path);
        if (!moveRemote(action, &error))
            return failed(error);
        ++summary->moved;
        return true;
    }
    case ActionKind::MoveLocal: {
        QString error;
        emitLog(tr("本地重命名"), action.fromPath + QStringLiteral(" → ") + action.path);
        if (!moveLocal(action, &error))
            return failed(error);
        ++summary->moved;
        return true;
    }
    case ActionKind::ResolveConflict:
        if (resolveConflict(action, summary))
            return true;
        return !m_cancelled.loadRelaxed();
    }
    return failed(tr("未知操作类型"));
}

} // namespace NutSync
