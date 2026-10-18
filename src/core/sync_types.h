#pragma once

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

namespace NutSync {

enum class EntryType {
    File,
    Directory,
};

enum class RemoteErrorClass {
    None,
    Network,
    Timeout,
    RateLimited,
    Authentication,
    Permission,
    NotFound,
    Conflict,
    Redirect,
    Protocol,
    Server,
    Quota,
    LocalIo,
    Cancelled,
    Other,
};

enum class ConflictPolicy {
    KeepBoth,     //!< Never discard data: keep a conflict copy for content conflicts.
    PreferLocal,  //!< Local changes win.
    PreferRemote, //!< Remote changes win.
    NewerWins,    //!< The entry with the newest modification time wins.
};

QString conflictPolicyToString(ConflictPolicy policy);
ConflictPolicy conflictPolicyFromString(const QString &value);
QString conflictPolicyDisplayName(ConflictPolicy policy);
QString rateLimitWindowDisplay(int windowMinutes);
QString rateLimitDescription(bool enabled, int windowMinutes, int maxRequests);

struct LocalEntry
{
    QString path;
    EntryType type = EntryType::File;
    qint64 size = 0;
    qint64 mtimeMs = 0;

    bool isValid() const { return !path.isEmpty(); }
};

struct RemoteEntry
{
    QString path;
    EntryType type = EntryType::File;
    qint64 size = 0;
    qint64 mtimeMs = 0;
    QString etag;

    bool isValid() const { return !path.isEmpty(); }
};

struct StateEntry
{
    QString path;
    EntryType type = EntryType::File;
    qint64 localSize = 0;
    qint64 localMtimeMs = 0;
    qint64 remoteSize = 0;
    qint64 remoteMtimeMs = 0;
    QString remoteEtag;
    qint64 syncedAtMs = 0;
};

struct SyncPair
{
    QString id;
    QString name;
    QString localPath;
    QString remoteUrl;
    QString backendType = QStringLiteral("webdav");
    QString username;
    QString password; //!< Transient. Persisted through the credential store.
    bool rememberPassword = true;
    QStringList excludePatterns;
    ConflictPolicy conflictPolicy = ConflictPolicy::KeepBoth;
    int intervalMinutes = 15;
    bool rateLimitEnabled = false;
    int rateLimitWindowMinutes = 60;
    int rateLimitMaxRequests = 10;
    bool watchLocalChanges = true;
    bool syncDeletions = false;
    bool enabled = true;
    bool syncOnStartup = true;
    QString lastStatus;
    QString lastMessage;
    QDateTime lastSync;
    QString lastError;

    bool isValid() const
    {
        return !id.isEmpty() && !name.isEmpty() && !localPath.isEmpty() && !remoteUrl.isEmpty();
    }
};

struct AppSettings
{
    QString language = QStringLiteral("system");
    QString proxyMode = QStringLiteral("system");
    QString proxyHost;
    int proxyPort = 8080;
    QString proxyUser;
    QString caBundlePath;
    QString logLevel = QStringLiteral("info");
    int retryCount = 2;
    int retryDelaySeconds = 30;
    bool notifications = true;
    bool closeToTray = true;
    bool autoSyncOnStartup = true;
    bool startMinimized = false;
};

struct AppConfig
{
    QList<SyncPair> pairs;
    AppSettings settings;
};

enum class ActionKind {
    Upload,
    Download,
    CreateRemoteDir,
    CreateLocalDir,
    DeleteRemote,
    DeleteLocal,
    ResolveConflict,
};

QString actionKindToString(ActionKind kind);

struct SyncAction
{
    ActionKind kind = ActionKind::Upload;
    QString path;
    LocalEntry local;
    RemoteEntry remote;
    StateEntry state;
    bool stateExists = false;
};

struct SyncPlan
{
    QList<SyncAction> actions;
    QList<StateEntry> baselineUpdates;
    QStringList removedStates;
    QStringList conflicts;
    int uploads = 0;
    int downloads = 0;
    int deletes = 0;
};

struct SyncSummary
{
    QString pairId;
    QString pairName;
    bool success = false;
    bool cancelled = false;
    int uploaded = 0;
    int downloaded = 0;
    int deletedLocal = 0;
    int deletedRemote = 0;
    int directoriesCreated = 0;
    int conflicts = 0;
    int skipped = 0;
    int errors = 0;
    QString message;
    QStringList errorDetails;
    QList<RemoteErrorClass> errorClasses;
    QStringList conflictPaths;
    qint64 elapsedMs = 0;
};

} // namespace NutSync

Q_DECLARE_METATYPE(NutSync::SyncSummary)
Q_DECLARE_METATYPE(NutSync::SyncPlan)
