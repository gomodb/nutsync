#pragma once

#include "core/remote_backend.h"
#include "core/sync_types.h"

#include <QAtomicInt>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QSet>

#include <functional>
#include <memory>

namespace NutSync {

class SyncStateStore;

struct TransferJob
{
    int index = 0;
    SyncAction action;
    QString localFile;
    QString expectedEtag;
    qint64 bytes = 0;
};

struct TransferOutcome
{
    int index = 0;
    bool success = false;
    QString error;
};

class SyncEngine : public QObject
{
    Q_OBJECT

public:
    using BackendFactory = std::function<std::unique_ptr<RemoteBackend>()>;

    explicit SyncEngine(SyncPair pair, QString stateDatabasePath = {}, QObject *parent = nullptr);

    void setBackendFactory(BackendFactory factory);
    void setRetryPolicy(int retryCount, int maxDelayMs);
    void setTransferLimit(int kbps);
    void setParallelTransfers(int count);
    void requestCancel();
    SyncSummary lastSummary() const;

    //! Blocking. Run this from a worker thread via QtConcurrent::run().
    void run();

signals:
    void started();
    void logMessage(const QString &message);
    void progress(int completed, int total, qint64 bytesDone, qint64 bytesTotal,
                  const QString &path);
    void finished(const NutSync::SyncSummary &summary);

private:
    bool scanLocal(QMap<QString, LocalEntry> *entries, QString *error);
    bool scanLocalDirectory(const QString &relativeDir, QMap<QString, LocalEntry> *entries,
                            QString *error);
    bool statLocal(const QString &relativePath, LocalEntry *entry, bool *exists) const;
    bool localDeleteStillSafe(const SyncAction &action) const;
    bool ensureTrashBatch(QString *error);
    bool includedInScope(const QString &path) const;
    void filterActionsForMode(SyncPlan *plan) const;
    void detectLocalMoves(SyncPlan *plan, const QHash<QString, StateEntry> &state) const;
    bool detectNameCollisions(QString *error) const;
    bool moveRemote(const SyncAction &action, QString *error);
    std::unique_ptr<RemoteBackend> createBackend(QString *error) const;
    bool prepareTransferJob(const SyncAction &action, int index, TransferJob *job, bool *skipped,
                            QString *error);
    void executeTransferBatch(const QList<TransferJob> &jobs, SyncSummary *summary);
    bool localSubtreeStillMatchesSnapshot(const QString &prefix) const;
    bool ensureLocalParent(const QString &relativePath, QString *error) const;
    bool verifyRemoteUnchanged(const SyncAction &action, QString *error,
                               QString *currentEtag = nullptr);
    bool recordSynced(const QString &path, QString *error);
    bool contentsEqual(const QString &localPath, const RemoteEntry &remote,
                       QString *temporaryRemoteFile, QString *error);
    bool resolveConflict(const SyncAction &action, SyncSummary *summary);
    bool resolveKeepBoth(const SyncAction &action, const QString &temporaryRemoteFile,
                         SyncSummary *summary);
    bool chooseConflictPath(const QString &path, QString *conflictPath, QString *error);
    bool doUpload(const SyncAction &action, const QString &localFile, const QString &remotePath,
                  const QString &expectedEtag, QString *error);
    bool doDownload(const SyncAction &action, const QString &remotePath, const QString &localFile,
                    QString *error);
    bool deleteRemote(const SyncAction &action, QString *error);
    bool deleteLocal(const SyncAction &action, QString *error);
    bool executeAction(const SyncAction &action, SyncSummary *summary);
    void emitLog(const QString &message, const QString &path = {});
    void addError(SyncSummary *summary, const QString &message);

    SyncPair m_pair;
    QString m_stateDatabasePath;
    BackendFactory m_backendFactory;
    int m_retryCount = 3;
    int m_retryBaseDelayMs = 1000;
    int m_retryMaxDelayMs = 30000;
    int m_transferLimitKbps = 0;
    int m_parallelTransfers = 1;
    QAtomicInt m_cancelled{0};
    QMap<QString, LocalEntry> m_localSnapshot;
    std::unique_ptr<RemoteBackend> m_backend;
    SyncStateStore *m_stateStore = nullptr;
    QSet<QString> m_excludedLocal;
    QSet<QString> m_excludedRemote;
    QMap<QString, RemoteEntry> m_remoteSnapshot;
    QString m_trashBatchPath;
    int m_completedActions = 0;
    int m_totalActions = 0;
    qint64 m_bytesDone = 0;
    qint64 m_bytesTotal = 0;
    mutable QMutex m_summaryMutex;
    SyncSummary m_lastSummary;
};

} // namespace NutSync
