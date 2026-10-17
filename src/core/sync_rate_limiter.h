#pragma once

#include "core/sync_types.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>

namespace NutSync {

/**
 * Persistent, per-pair limiter for synchronization starts.
 *
 * The limiter stores one timestamp per started synchronization. Automatic
 * triggers are rejected once the configured quota is exhausted; manual starts
 * can bypass the check but still consume quota. Timestamps survive restarts so
 * a server-side request budget cannot be reset by restarting NutSync.
 */
class SyncRateLimiter
{
public:
    explicit SyncRateLimiter(QString storagePath = {});

    bool load(QString *error = nullptr);
    bool save(QString *error = nullptr) const;

    bool isAllowed(const SyncPair &pair, const QDateTime &now,
                   QDateTime *nextAllowed = nullptr) const;
    bool recordStart(const SyncPair &pair, const QDateTime &now,
                     QString *error = nullptr);
    int requestCount(const SyncPair &pair, const QDateTime &now) const;
    bool clear(const QString &pairId, QString *error = nullptr);

    QString storagePath() const;

private:
    QList<qint64> recentRequests(const QString &pairId, int windowMinutes,
                                 const QDateTime &now) const;
    void pruneAll();

    QString m_storagePath;
    QHash<QString, QList<qint64>> m_history;
};

} // namespace NutSync
