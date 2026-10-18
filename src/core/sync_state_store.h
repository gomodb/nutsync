#pragma once

#include "core/sync_types.h"

#include <QHash>
#include <QSqlDatabase>
#include <QString>

namespace NutSync {

class SyncStateStore
{
public:
    explicit SyncStateStore(QString databasePath = {});
    ~SyncStateStore();

    SyncStateStore(const SyncStateStore &) = delete;
    SyncStateStore &operator=(const SyncStateStore &) = delete;

    bool open(QString *error = nullptr);
    void close();

    bool load(const QString &pairId, QHash<QString, StateEntry> *entries,
              QString *error = nullptr) const;
    bool upsert(const QString &pairId, const StateEntry &entry, QString *error = nullptr);
    bool upsertMany(const QString &pairId, const QList<StateEntry> &entries,
                    QString *error = nullptr);
    bool removePath(const QString &pairId, const QString &path, QString *error = nullptr);
    bool removePrefix(const QString &pairId, const QString &path, QString *error = nullptr);
    bool clearPair(const QString &pairId, QString *error = nullptr);

private:
    bool ensureSchema(QString *error);
    bool backupDatabase(const QString &suffix, QString *error);
    static QString escapeLike(const QString &value);

    QString m_databasePath;
    QString m_connectionName;
    QSqlDatabase m_database;
    bool m_open = false;
};

} // namespace NutSync
