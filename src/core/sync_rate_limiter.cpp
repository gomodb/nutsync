#include "core/sync_rate_limiter.h"

#include "core/app_paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSaveFile>

#include <algorithm>

namespace NutSync {

namespace {
constexpr qint64 kRetentionMs = 7LL * 24 * 60 * 60 * 1000;
}

SyncRateLimiter::SyncRateLimiter(QString storagePath)
    : m_storagePath(storagePath.isEmpty()
                        ? AppPaths::dataDir() + QStringLiteral("/rate_limits.json")
                        : std::move(storagePath))
{
}

QString SyncRateLimiter::storagePath() const
{
    return m_storagePath;
}

bool SyncRateLimiter::load(QString *error)
{
    m_history.clear();
    QFile file(m_storagePath);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("无法读取请求频率记录：%1").arg(file.errorString());
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QObject::tr("请求频率记录格式错误：%1").arg(parseError.errorString());
        }
        return false;
    }

    const QJsonObject pairs = document.object().value(QStringLiteral("pairs")).toObject();
    for (auto it = pairs.begin(); it != pairs.end(); ++it) {
        QList<qint64> timestamps;
        const QJsonArray values = it.value().toArray();
        for (const auto &value : values) {
            const qint64 timestamp = static_cast<qint64>(value.toDouble());
            if (timestamp > 0)
                timestamps.append(timestamp);
        }
        std::sort(timestamps.begin(), timestamps.end());
        if (!timestamps.isEmpty())
            m_history.insert(it.key(), timestamps);
    }
    pruneAll();
    return true;
}

bool SyncRateLimiter::save(QString *error) const
{
    QDir().mkpath(QFileInfo(m_storagePath).absolutePath());

    QJsonObject pairs;
    for (auto it = m_history.cbegin(); it != m_history.cend(); ++it) {
        if (it.value().isEmpty())
            continue;
        QJsonArray values;
        for (qint64 timestamp : it.value())
            values.append(static_cast<double>(timestamp));
        pairs.insert(it.key(), values);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("pairs"), pairs);

    QSaveFile file(m_storagePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QObject::tr("无法写入请求频率记录：%1").arg(file.errorString());
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (error)
            *error = QObject::tr("保存请求频率记录失败：%1").arg(file.errorString());
        return false;
    }
    return true;
}

QList<qint64> SyncRateLimiter::recentRequests(const QString &pairId, int windowMinutes,
                                              const QDateTime &now) const
{
    QList<qint64> result;
    if (windowMinutes <= 0)
        return result;
    const qint64 cutoff = now.toMSecsSinceEpoch() - qint64(windowMinutes) * 60 * 1000;
    for (qint64 timestamp : m_history.value(pairId)) {
        if (timestamp > cutoff)
            result.append(timestamp);
    }
    return result;
}

int SyncRateLimiter::requestCount(const SyncPair &pair, const QDateTime &now) const
{
    return recentRequests(pair.id, pair.rateLimitWindowMinutes, now).size();
}

bool SyncRateLimiter::isAllowed(const SyncPair &pair, const QDateTime &now,
                                QDateTime *nextAllowed) const
{
    if (nextAllowed)
        *nextAllowed = {};
    if (!pair.rateLimitEnabled || pair.rateLimitMaxRequests <= 0
        || pair.rateLimitWindowMinutes <= 0) {
        return true;
    }

    QList<qint64> recent = m_history.value(pair.id);
    const qint64 windowMs = qint64(pair.rateLimitWindowMinutes) * 60 * 1000;
    const qint64 nowMs = now.toMSecsSinceEpoch();
    const qint64 cutoff = nowMs - windowMs;
    recent.erase(std::remove_if(recent.begin(), recent.end(),
                                [cutoff](qint64 timestamp) {
                                    return timestamp <= cutoff;
                                }),
                 recent.end());
    if (recent.size() < pair.rateLimitMaxRequests)
        return true;

    if (nextAllowed) {
        std::sort(recent.begin(), recent.end());
        const qint64 oldest = recent.constFirst();
        *nextAllowed = QDateTime::fromMSecsSinceEpoch(oldest + windowMs).toUTC();
    }
    return false;
}

bool SyncRateLimiter::recordStart(const SyncPair &pair, const QDateTime &now, QString *error)
{
    if (!pair.rateLimitEnabled || pair.rateLimitWindowMinutes <= 0
        || pair.rateLimitMaxRequests <= 0) {
        return true;
    }

    const qint64 nowMs = now.toMSecsSinceEpoch();
    const qint64 cutoff = nowMs - qint64(pair.rateLimitWindowMinutes) * 60 * 1000;
    QList<qint64> &timestamps = m_history[pair.id];
    timestamps.erase(std::remove_if(timestamps.begin(), timestamps.end(),
                                    [cutoff](qint64 timestamp) {
                                        return timestamp <= cutoff;
                                    }),
                     timestamps.end());
    timestamps.append(nowMs);
    return save(error);
}

bool SyncRateLimiter::clear(const QString &pairId, QString *error)
{
    if (m_history.remove(pairId) == 0)
        return true;
    return save(error);
}

void SyncRateLimiter::pruneAll()
{
    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - kRetentionMs;
    for (auto it = m_history.begin(); it != m_history.end();) {
        QList<qint64> &values = it.value();
        values.erase(std::remove_if(values.begin(), values.end(),
                                    [cutoff](qint64 timestamp) {
                                        return timestamp <= cutoff;
                                    }),
                     values.end());
        if (values.isEmpty())
            it = m_history.erase(it);
        else
            ++it;
    }
}

} // namespace NutSync
