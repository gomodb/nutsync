#include "core/sync_state_store.h"

#include "core/app_paths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace NutSync {

namespace {

QString queryError(const QSqlQuery &query)
{
    return query.lastError().text();
}

} // namespace

SyncStateStore::SyncStateStore(QString databasePath)
    : m_databasePath(databasePath.isEmpty() ? AppPaths::stateDatabase() : std::move(databasePath))
    , m_connectionName(QStringLiteral("nutsync-state-%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

SyncStateStore::~SyncStateStore()
{
    close();
}

bool SyncStateStore::open(QString *error)
{
    if (m_open)
        return true;

    const QString directory = QFileInfo(m_databasePath).absolutePath();
    if (!QDir().mkpath(directory)) {
        if (error)
            *error = QObject::tr("无法创建状态目录“%1”").arg(directory);
        return false;
    }

    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(m_databasePath);
    if (!m_database.open()) {
        if (error)
            *error = QObject::tr("无法打开同步状态数据库：%1").arg(m_database.lastError().text());
        m_database = {};
        QSqlDatabase::removeDatabase(m_connectionName);
        return false;
    }

    QSqlQuery pragma(m_database);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    pragma.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    pragma.finish();

    QSqlQuery check(m_database);
    if (check.exec(QStringLiteral("PRAGMA quick_check")) && check.next()
        && check.value(0).toString() != QLatin1String("ok")) {
        if (error)
            *error = QObject::tr("同步状态数据库损坏：%1").arg(check.value(0).toString());
        check.finish();
        close();
        return false;
    }
    check.finish();

    QFile::setPermissions(m_databasePath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (!ensureSchema(error)) {
        close();
        return false;
    }
    m_open = true;
    return true;
}

void SyncStateStore::close()
{
    if (m_database.isValid()) {
        if (m_database.isOpen())
            m_database.close();
        m_database = {};
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    m_open = false;
}

bool SyncStateStore::ensureSchema(QString *error)
{
    constexpr int currentVersion = 2;
    QSqlQuery versionQuery(m_database);
    int version = 0;
    if (versionQuery.exec(QStringLiteral("PRAGMA user_version")) && versionQuery.next())
        version = versionQuery.value(0).toInt();
    versionQuery.finish();
    if (version > currentVersion) {
        if (error)
            *error = QObject::tr("同步状态数据库版本 %1 高于当前支持版本 %2")
                         .arg(version)
                         .arg(currentVersion);
        return false;
    }
    if (version > 0 && version < currentVersion) {
        if (!backupDatabase(QStringLiteral("pre-migration-v%1").arg(version), error))
            return false;
    }

    QSqlQuery query(m_database);
    const QString sql = QStringLiteral("CREATE TABLE IF NOT EXISTS sync_state ("
                                       "  pair_id TEXT NOT NULL,"
                                       "  path TEXT NOT NULL,"
                                       "  entry_type INTEGER NOT NULL,"
                                       "  local_size INTEGER NOT NULL,"
                                       "  local_mtime INTEGER NOT NULL,"
                                       "  remote_size INTEGER NOT NULL,"
                                       "  remote_mtime INTEGER NOT NULL,"
                                       "  remote_etag TEXT NOT NULL DEFAULT '',"
                                       "  synced_at INTEGER NOT NULL,"
                                       "  PRIMARY KEY (pair_id, path)"
                                       ")");
    if (!query.exec(sql)) {
        if (error)
            *error = QObject::tr("创建同步状态表失败：%1").arg(queryError(query));
        return false;
    }
    const QString conflictsSql = QStringLiteral("CREATE TABLE IF NOT EXISTS conflicts ("
                                                "  pair_id TEXT NOT NULL,"
                                                "  path TEXT NOT NULL,"
                                                "  copy_path TEXT NOT NULL DEFAULT '',"
                                                "  detected_at INTEGER NOT NULL,"
                                                "  PRIMARY KEY (pair_id, path, copy_path)"
                                                ")");
    if (!query.exec(conflictsSql)) {
        if (error)
            *error = QObject::tr("创建冲突记录表失败：%1").arg(queryError(query));
        return false;
    }
    if (version < currentVersion && !query.exec(QStringLiteral("PRAGMA user_version=2"))) {
        if (error)
            *error = QObject::tr("设置状态数据库版本失败：%1").arg(queryError(query));
        return false;
    }
    return true;
}

bool SyncStateStore::backupDatabase(const QString &suffix, QString *error)
{
    const QString backup = m_databasePath + QStringLiteral(".bak-") + suffix + QStringLiteral("-")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QSqlQuery query(m_database);
    QString escaped = backup;
    escaped.replace(QLatin1Char('\''), QLatin1String("''"));
    if (!query.exec(QStringLiteral("VACUUM INTO '%1'").arg(escaped))) {
        if (error)
            *error = QObject::tr("备份状态数据库失败：%1").arg(queryError(query));
        return false;
    }
    QFile::setPermissions(backup, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

bool SyncStateStore::load(const QString &pairId, QHash<QString, StateEntry> *entries,
                          QString *error) const
{
    if (!entries) {
        if (error)
            *error = QObject::tr("内部错误：状态输出为空");
        return false;
    }
    entries->clear();
    if (!m_open)
        return true;

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT path, entry_type, local_size, local_mtime, remote_size, remote_mtime,"
        " remote_etag, synced_at FROM sync_state WHERE pair_id = ?"));
    query.addBindValue(pairId);
    if (!query.exec()) {
        if (error)
            *error = QObject::tr("读取同步状态失败：%1").arg(queryError(query));
        return false;
    }
    while (query.next()) {
        StateEntry entry;
        entry.path = query.value(0).toString();
        entry.type = static_cast<EntryType>(query.value(1).toInt());
        entry.localSize = query.value(2).toLongLong();
        entry.localMtimeMs = query.value(3).toLongLong();
        entry.remoteSize = query.value(4).toLongLong();
        entry.remoteMtimeMs = query.value(5).toLongLong();
        entry.remoteEtag = query.value(6).toString();
        entry.syncedAtMs = query.value(7).toLongLong();
        entries->insert(entry.path, entry);
    }
    return true;
}

bool SyncStateStore::upsert(const QString &pairId, const StateEntry &entry, QString *error)
{
    if (!m_open)
        return true;
    QSqlQuery query(m_database);
    query.prepare(
        QStringLiteral("INSERT INTO sync_state (pair_id, path, entry_type, local_size, local_mtime,"
                       " remote_size, remote_mtime, remote_etag, synced_at)"
                       " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"
                       " ON CONFLICT(pair_id, path) DO UPDATE SET"
                       " entry_type=excluded.entry_type, local_size=excluded.local_size,"
                       " local_mtime=excluded.local_mtime, remote_size=excluded.remote_size,"
                       " remote_mtime=excluded.remote_mtime, remote_etag=excluded.remote_etag,"
                       " synced_at=excluded.synced_at"));
    query.addBindValue(pairId);
    query.addBindValue(entry.path);
    query.addBindValue(static_cast<int>(entry.type));
    query.addBindValue(entry.localSize);
    query.addBindValue(entry.localMtimeMs);
    query.addBindValue(entry.remoteSize);
    query.addBindValue(entry.remoteMtimeMs);
    query.addBindValue(entry.remoteEtag.isNull() ? QStringLiteral("") : entry.remoteEtag);
    query.addBindValue(entry.syncedAtMs > 0 ? entry.syncedAtMs
                                            : QDateTime::currentMSecsSinceEpoch());
    if (!query.exec()) {
        if (error)
            *error = QObject::tr("写入同步状态失败：%1").arg(queryError(query));
        return false;
    }
    return true;
}

bool SyncStateStore::upsertMany(const QString &pairId, const QList<StateEntry> &entries,
                                QString *error)
{
    if (!m_open || entries.isEmpty())
        return true;
    if (!m_database.transaction()) {
        if (error)
            *error = QObject::tr("开始状态数据库事务失败：%1").arg(m_database.lastError().text());
        return false;
    }
    for (const StateEntry &entry : entries) {
        if (!upsert(pairId, entry, error)) {
            m_database.rollback();
            return false;
        }
    }
    if (!m_database.commit()) {
        if (error)
            *error = QObject::tr("提交状态数据库事务失败：%1").arg(m_database.lastError().text());
        m_database.rollback();
        return false;
    }
    return true;
}

bool SyncStateStore::removePath(const QString &pairId, const QString &path, QString *error)
{
    if (!m_open)
        return true;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM sync_state WHERE pair_id = ? AND path = ?"));
    query.addBindValue(pairId);
    query.addBindValue(path);
    if (!query.exec()) {
        if (error)
            *error = QObject::tr("移除同步状态失败：%1").arg(queryError(query));
        return false;
    }
    return true;
}

QString SyncStateStore::escapeLike(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    escaped.replace(QLatin1Char('%'), QLatin1String("\\%"));
    escaped.replace(QLatin1Char('_'), QLatin1String("\\_"));
    return escaped;
}

bool SyncStateStore::removePrefix(const QString &pairId, const QString &path, QString *error)
{
    if (!m_open)
        return true;
    if (path.isEmpty())
        return clearPair(pairId, error);

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM sync_state WHERE pair_id = ?"
                                 " AND (path = ? OR path LIKE ? ESCAPE '\\')"));
    query.addBindValue(pairId);
    query.addBindValue(path);
    query.addBindValue(escapeLike(path) + QStringLiteral("/%"));
    if (!query.exec()) {
        if (error)
            *error = QObject::tr("移除同步状态失败：%1").arg(queryError(query));
        return false;
    }
    return true;
}

bool SyncStateStore::clearPair(const QString &pairId, QString *error)
{
    if (!m_open)
        return true;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM sync_state WHERE pair_id = ?"));
    query.addBindValue(pairId);
    if (!query.exec()) {
        if (error)
            *error = QObject::tr("清空同步状态失败：%1").arg(queryError(query));
        return false;
    }
    return true;
}

bool SyncStateStore::replaceConflicts(const QString &pairId, const QList<ConflictRecord> &records,
                                      QString *error)
{
    if (!m_open)
        return true;
    if (!m_database.transaction()) {
        if (error)
            *error = QObject::tr("开始冲突记录事务失败：%1").arg(m_database.lastError().text());
        return false;
    }
    QSqlQuery remove(m_database);
    remove.prepare(QStringLiteral("DELETE FROM conflicts WHERE pair_id = ?"));
    remove.addBindValue(pairId);
    if (!remove.exec()) {
        m_database.rollback();
        if (error)
            *error = QObject::tr("清理冲突记录失败：%1").arg(queryError(remove));
        return false;
    }
    for (const ConflictRecord &record : records) {
        QSqlQuery insert(m_database);
        insert.prepare(QStringLiteral("INSERT OR REPLACE INTO conflicts"
                                      " (pair_id, path, copy_path, detected_at)"
                                      " VALUES (?, ?, ?, ?)"));
        insert.addBindValue(pairId);
        insert.addBindValue(record.path);
        insert.addBindValue(record.copyPath);
        insert.addBindValue(QDateTime::currentMSecsSinceEpoch());
        if (!insert.exec()) {
            m_database.rollback();
            if (error)
                *error = QObject::tr("写入冲突记录失败：%1").arg(queryError(insert));
            return false;
        }
    }
    if (!m_database.commit()) {
        m_database.rollback();
        if (error)
            *error = QObject::tr("提交冲突记录事务失败：%1").arg(m_database.lastError().text());
        return false;
    }
    return true;
}

bool SyncStateStore::loadConflicts(const QString &pairId, QList<ConflictRecord> *records,
                                   QString *error) const
{
    if (!records)
        return false;
    records->clear();
    if (!m_open)
        return true;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT path, copy_path FROM conflicts WHERE pair_id = ? ORDER BY path, copy_path"));
    query.addBindValue(pairId);
    if (!query.exec()) {
        if (error)
            *error = QObject::tr("读取冲突记录失败：%1").arg(queryError(query));
        return false;
    }
    while (query.next())
        records->append({query.value(0).toString(), query.value(1).toString()});
    return true;
}

bool SyncStateStore::removeConflict(const QString &pairId, const QString &path,
                                    const QString &copyPath, QString *error)
{
    if (!m_open)
        return true;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM conflicts"
                                 " WHERE pair_id = ? AND path = ? AND copy_path = ?"));
    query.addBindValue(pairId);
    query.addBindValue(path);
    query.addBindValue(copyPath);
    if (!query.exec()) {
        if (error)
            *error = QObject::tr("移除冲突记录失败：%1").arg(queryError(query));
        return false;
    }
    return true;
}

} // namespace NutSync
