#pragma once

#include "core/credential_store.h"
#include "core/sync_types.h"

#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QString>

namespace NutSync {

class SyncEngine;

class SyncController : public QObject
{
    Q_OBJECT

public:
    explicit SyncController(QString stateDatabasePath, QObject *parent = nullptr);
    ~SyncController() override;

    void enqueue(const SyncPair &pair);
    void setSettings(const AppSettings &settings);
    bool isBusy() const;
    bool contains(const QString &pairId) const;
    QString currentPairId() const;
    int pendingCount() const;

    void cancelCurrent();
    void clearQueue();

signals:
    void pairStarted(const QString &pairId);
    void logMessage(const QString &pairId, const QString &message);
    void progress(const QString &pairId, int completed, int total, qint64 bytesDone,
                  qint64 bytesTotal, const QString &path);
    void pairFinished(const NutSync::SyncSummary &summary);
    void busyChanged(bool busy);
    void queueChanged(int pending);
    void oauthTokensRefreshed(const QString &pairId, const QString &refreshToken,
                              qint64 expiresAtMs);

private:
    void startNext();
    void onEngineFinished(const SyncSummary &summary);
    void onWorkerFinished();

    QString m_stateDatabasePath;
    AppSettings m_settings;
    CredentialStore m_credentials;
    QQueue<SyncPair> m_queue;
    SyncPair m_currentPair;
    QString m_currentPairId;
    QPointer<SyncEngine> m_engine;
    QFutureWatcher<void> m_watcher;
    SyncSummary m_pendingSummary;
    bool m_hasPendingSummary = false;
    bool m_busy = false;
};

} // namespace NutSync
