#include "core/config_store.h"

#include "core/app_paths.h"
#include "core/path_utils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <QUuid>

namespace NutSync {

namespace {

QJsonObject pairToJson(const SyncPair &pair)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), pair.id);
    obj.insert(QStringLiteral("name"), pair.name);
    obj.insert(QStringLiteral("localPath"), pair.localPath);
    QUrl remoteUrl(pair.remoteUrl);
    if (remoteUrl.isValid() && !remoteUrl.scheme().isEmpty())
        remoteUrl.setUserInfo({});
    obj.insert(QStringLiteral("remoteUrl"), remoteUrl.toString());
    obj.insert(QStringLiteral("backendType"), pair.backendType);
    obj.insert(QStringLiteral("username"), pair.username);
    obj.insert(QStringLiteral("authMode"), pair.authMode);
    obj.insert(QStringLiteral("clientCertificatePath"), pair.clientCertificatePath);
    obj.insert(QStringLiteral("clientKeyPath"), pair.clientKeyPath);
    obj.insert(QStringLiteral("chunkedUpload"), pair.chunkedUpload);
    obj.insert(QStringLiteral("chunkSizeMb"), pair.chunkSizeMb);
    obj.insert(QStringLiteral("rememberPassword"), pair.rememberPassword);
    obj.insert(QStringLiteral("excludePatterns"), QJsonArray::fromStringList(pair.excludePatterns));
    obj.insert(QStringLiteral("includePaths"), QJsonArray::fromStringList(pair.includePaths));
    obj.insert(QStringLiteral("syncMode"), syncModeToString(pair.syncMode));
    obj.insert(QStringLiteral("conflictPolicy"), conflictPolicyToString(pair.conflictPolicy));
    obj.insert(QStringLiteral("intervalMinutes"), pair.intervalMinutes);
    obj.insert(QStringLiteral("rateLimitEnabled"), pair.rateLimitEnabled);
    obj.insert(QStringLiteral("rateLimitWindowMinutes"), pair.rateLimitWindowMinutes);
    obj.insert(QStringLiteral("rateLimitMaxRequests"), pair.rateLimitMaxRequests);
    obj.insert(QStringLiteral("watchLocalChanges"), pair.watchLocalChanges);
    obj.insert(QStringLiteral("syncDeletions"), pair.syncDeletions);
    obj.insert(QStringLiteral("trashDeletedFiles"), pair.trashDeletedFiles);
    obj.insert(QStringLiteral("trashRetentionDays"), pair.trashRetentionDays);
    obj.insert(QStringLiteral("enabled"), pair.enabled);
    obj.insert(QStringLiteral("paused"), pair.paused);
    obj.insert(QStringLiteral("syncOnStartup"), pair.syncOnStartup);
    obj.insert(QStringLiteral("lastStatus"), pair.lastStatus);
    obj.insert(QStringLiteral("lastMessage"), pair.lastMessage);
    obj.insert(QStringLiteral("lastSync"),
               pair.lastSync.isValid() ? pair.lastSync.toUTC().toString(Qt::ISODateWithMs)
                                       : QString());
    obj.insert(QStringLiteral("lastError"), pair.lastError);
    return obj;
}

SyncPair pairFromJson(const QJsonObject &obj)
{
    SyncPair pair;
    pair.id = obj.value(QStringLiteral("id")).toString();
    if (pair.id.isEmpty())
        pair.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    pair.name = obj.value(QStringLiteral("name")).toString();
    pair.localPath = obj.value(QStringLiteral("localPath")).toString();
    pair.remoteUrl = obj.value(QStringLiteral("remoteUrl")).toString();
    pair.backendType = obj.value(QStringLiteral("backendType")).toString();
    if (pair.backendType.isEmpty())
        pair.backendType = QStringLiteral("webdav");
    pair.username = obj.value(QStringLiteral("username")).toString();
    pair.authMode = obj.value(QStringLiteral("authMode")).toString(QStringLiteral("basic"));
    if (pair.authMode != QLatin1String("digest") && pair.authMode != QLatin1String("bearer"))
        pair.authMode = QStringLiteral("basic");
    pair.clientCertificatePath = obj.value(QStringLiteral("clientCertificatePath")).toString();
    pair.clientKeyPath = obj.value(QStringLiteral("clientKeyPath")).toString();
    pair.chunkedUpload = obj.value(QStringLiteral("chunkedUpload")).toBool(false);
    pair.chunkSizeMb = qBound(1, obj.value(QStringLiteral("chunkSizeMb")).toInt(10), 100);
    QUrl remoteUrl(pair.remoteUrl);
    if (!remoteUrl.userName().isEmpty() || !remoteUrl.password().isEmpty()) {
        if (pair.username.isEmpty())
            pair.username = remoteUrl.userName();
        pair.password = remoteUrl.password();
        remoteUrl.setUserInfo({});
        pair.remoteUrl = remoteUrl.toString();
    }
    pair.rememberPassword = obj.value(QStringLiteral("rememberPassword")).toBool(true);
    const QJsonArray excludes = obj.value(QStringLiteral("excludePatterns")).toArray();
    for (const auto &value : excludes) {
        const QString pattern = value.toString().trimmed();
        if (!pattern.isEmpty())
            pair.excludePatterns.append(pattern);
    }
    const QJsonArray includes = obj.value(QStringLiteral("includePaths")).toArray();
    for (const auto &value : includes) {
        bool ok = false;
        QString path = value.toString().trimmed();
        while (path.startsWith(QLatin1Char('/')))
            path.remove(0, 1);
        path = PathUtils::normalizeRelativePath(path, &ok);
        if (ok && !path.isEmpty() && !pair.includePaths.contains(path))
            pair.includePaths.append(path);
    }
    pair.syncMode = syncModeFromString(obj.value(QStringLiteral("syncMode")).toString());
    pair.conflictPolicy =
        conflictPolicyFromString(obj.value(QStringLiteral("conflictPolicy")).toString());
    pair.intervalMinutes = obj.value(QStringLiteral("intervalMinutes")).toInt(15);
    if (pair.intervalMinutes < 0)
        pair.intervalMinutes = 0;
    pair.rateLimitEnabled = obj.value(QStringLiteral("rateLimitEnabled")).toBool(false);
    pair.rateLimitWindowMinutes = obj.value(QStringLiteral("rateLimitWindowMinutes")).toInt(60);
    pair.rateLimitMaxRequests = obj.value(QStringLiteral("rateLimitMaxRequests")).toInt(10);
    pair.rateLimitWindowMinutes = qBound(1, pair.rateLimitWindowMinutes, 10080);
    pair.rateLimitMaxRequests = qBound(1, pair.rateLimitMaxRequests, 100000);
    pair.watchLocalChanges = obj.value(QStringLiteral("watchLocalChanges")).toBool(true);
    pair.syncDeletions = obj.value(QStringLiteral("syncDeletions")).toBool(false);
    pair.trashDeletedFiles = obj.value(QStringLiteral("trashDeletedFiles")).toBool(true);
    pair.trashRetentionDays =
        qBound(0, obj.value(QStringLiteral("trashRetentionDays")).toInt(30), 3650);
    pair.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
    pair.paused = obj.value(QStringLiteral("paused")).toBool(false);
    pair.syncOnStartup = obj.value(QStringLiteral("syncOnStartup")).toBool(true);
    pair.lastStatus = obj.value(QStringLiteral("lastStatus")).toString();
    pair.lastMessage = obj.value(QStringLiteral("lastMessage")).toString();
    const QString lastSync = obj.value(QStringLiteral("lastSync")).toString();
    if (!lastSync.isEmpty())
        pair.lastSync = QDateTime::fromString(lastSync, Qt::ISODateWithMs);
    pair.lastError = obj.value(QStringLiteral("lastError")).toString();
    return pair;
}

QJsonObject settingsToJson(const AppSettings &settings)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("language"), settings.language);
    obj.insert(QStringLiteral("proxyMode"), settings.proxyMode);
    obj.insert(QStringLiteral("proxyHost"), settings.proxyHost);
    obj.insert(QStringLiteral("proxyPort"), settings.proxyPort);
    obj.insert(QStringLiteral("proxyUser"), settings.proxyUser);
    obj.insert(QStringLiteral("caBundlePath"), settings.caBundlePath);
    obj.insert(QStringLiteral("logLevel"), settings.logLevel);
    obj.insert(QStringLiteral("logMaxSizeMb"), settings.logMaxSizeMb);
    obj.insert(QStringLiteral("logRetentionFiles"), settings.logRetentionFiles);
    obj.insert(QStringLiteral("retryCount"), settings.retryCount);
    obj.insert(QStringLiteral("retryDelaySeconds"), settings.retryDelaySeconds);
    obj.insert(QStringLiteral("transferLimitKbps"), settings.transferLimitKbps);
    obj.insert(QStringLiteral("parallelTransfers"), settings.parallelTransfers);
    obj.insert(QStringLiteral("notifications"), settings.notifications);
    obj.insert(QStringLiteral("closeToTray"), settings.closeToTray);
    obj.insert(QStringLiteral("autoSyncOnStartup"), settings.autoSyncOnStartup);
    obj.insert(QStringLiteral("syncOnNetworkRecovery"), settings.syncOnNetworkRecovery);
    obj.insert(QStringLiteral("startMinimized"), settings.startMinimized);
    return obj;
}

AppSettings settingsFromJson(const QJsonObject &obj)
{
    AppSettings settings;
    const QString language = obj.value(QStringLiteral("language")).toString();
    settings.language = (language == QLatin1String("en") || language == QLatin1String("zh_CN"))
        ? language
        : QStringLiteral("system");
    const QString proxyMode = obj.value(QStringLiteral("proxyMode")).toString();
    settings.proxyMode =
        (proxyMode == QLatin1String("none") || proxyMode == QLatin1String("manual"))
        ? proxyMode
        : QStringLiteral("system");
    settings.proxyHost = obj.value(QStringLiteral("proxyHost")).toString();
    settings.proxyPort = qBound(1, obj.value(QStringLiteral("proxyPort")).toInt(8080), 65535);
    settings.proxyUser = obj.value(QStringLiteral("proxyUser")).toString();
    settings.caBundlePath = obj.value(QStringLiteral("caBundlePath")).toString();
    const QString logLevel = obj.value(QStringLiteral("logLevel")).toString();
    settings.logLevel = (logLevel == QLatin1String("debug") || logLevel == QLatin1String("warning")
                         || logLevel == QLatin1String("error"))
        ? logLevel
        : QStringLiteral("info");
    settings.logMaxSizeMb = qBound(1, obj.value(QStringLiteral("logMaxSizeMb")).toInt(2), 100);
    settings.logRetentionFiles =
        qBound(1, obj.value(QStringLiteral("logRetentionFiles")).toInt(3), 10);
    settings.retryCount = qBound(0, obj.value(QStringLiteral("retryCount")).toInt(2), 5);
    settings.retryDelaySeconds =
        qBound(5, obj.value(QStringLiteral("retryDelaySeconds")).toInt(30), 600);
    settings.transferLimitKbps =
        qBound(0, obj.value(QStringLiteral("transferLimitKbps")).toInt(0), 102400);
    settings.parallelTransfers =
        qBound(1, obj.value(QStringLiteral("parallelTransfers")).toInt(1), 4);
    settings.notifications = obj.value(QStringLiteral("notifications")).toBool(true);
    settings.closeToTray = obj.value(QStringLiteral("closeToTray")).toBool(true);
    settings.autoSyncOnStartup = obj.value(QStringLiteral("autoSyncOnStartup")).toBool(true);
    settings.syncOnNetworkRecovery =
        obj.value(QStringLiteral("syncOnNetworkRecovery")).toBool(true);
    settings.startMinimized = obj.value(QStringLiteral("startMinimized")).toBool(false);
    return settings;
}

} // namespace

ConfigStore::ConfigStore(QString filePath)
    : m_filePath(filePath.isEmpty() ? AppPaths::configFile() : std::move(filePath))
{
}

QString ConfigStore::filePath() const
{
    return m_filePath;
}

bool ConfigStore::load(AppConfig *config, QString *error) const
{
    if (!config)
        return false;
    *config = AppConfig{};

    QFile file(m_filePath);
    if (!file.exists())
        return true;

    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("无法读取配置文件 %1：%2").arg(m_filePath, file.errorString());
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = QObject::tr("配置文件格式错误（偏移 %1）：%2")
                         .arg(parseError.offset)
                         .arg(parseError.errorString());
        return false;
    }
    if (!doc.isObject()) {
        if (error)
            *error = QObject::tr("配置文件根元素不是 JSON 对象");
        return false;
    }

    const QJsonObject root = doc.object();
    const int version = root.value(QStringLiteral("version")).toInt(1);
    if (version > 1) {
        if (error)
            *error = QObject::tr("配置文件版本 %1 高于当前支持版本 1").arg(version);
        return false;
    }
    config->settings = settingsFromJson(root.value(QStringLiteral("settings")).toObject());
    const QJsonArray pairs = root.value(QStringLiteral("pairs")).toArray();
    QSet<QString> seenIds;
    for (const auto &value : pairs) {
        if (!value.isObject())
            continue;
        SyncPair pair = pairFromJson(value.toObject());
        if (pair.id.isEmpty() || seenIds.contains(pair.id)) {
            pair.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        seenIds.insert(pair.id);
        config->pairs.append(pair);
    }
    return true;
}

bool ConfigStore::save(const AppConfig &config, QString *error) const
{
    QJsonArray pairs;
    for (const SyncPair &pair : config.pairs)
        pairs.append(pairToJson(pair));

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("settings"), settingsToJson(config.settings));
    root.insert(QStringLiteral("pairs"), pairs);

    const QString dir = QFileInfo(m_filePath).absolutePath();
    if (!QDir().mkpath(dir)) {
        if (error)
            *error = QObject::tr("无法创建配置目录 %1").arg(dir);
        return false;
    }

    QFile current(m_filePath);
    if (current.exists()) {
        const QString backup = m_filePath + QStringLiteral(".bak");
        QFile::remove(backup);
        QFile::copy(m_filePath, backup);
        QFile::setPermissions(backup, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }

    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QObject::tr("无法写入配置文件 %1：%2").arg(m_filePath, file.errorString());
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = QObject::tr("保存配置文件失败：%1").arg(file.errorString());
        return false;
    }
    return true;
}

} // namespace NutSync
