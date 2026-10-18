#include "test_config_store.h"

#include "core/config_store.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QTemporaryDir>

namespace NutSync::Test {

void TestConfigStore::stripsCredentialsEmbeddedInUrls()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("config.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({
        "version": 1,
        "settings": {},
        "pairs": [{
            "id": "p1",
            "name": "Pair",
            "localPath": "/tmp/local",
            "remoteUrl": "https://alice:secret@example.com/dav/",
            "rememberPassword": true
        }]
    })");
    file.close();

    ConfigStore store(path);
    AppConfig config;
    QString error;
    QVERIFY2(store.load(&config, &error), qPrintable(error));
    QCOMPARE(config.pairs.size(), 1);
    QCOMPARE(config.pairs.at(0).username, QStringLiteral("alice"));
    QCOMPARE(config.pairs.at(0).password, QStringLiteral("secret"));
    QVERIFY(!config.pairs.at(0).remoteUrl.contains(QStringLiteral("alice")));
    QVERIFY(!config.pairs.at(0).remoteUrl.contains(QStringLiteral("secret")));
    QCOMPARE(config.pairs.at(0).rateLimitEnabled, false);
    QCOMPARE(config.pairs.at(0).rateLimitWindowMinutes, 60);
    QCOMPARE(config.pairs.at(0).rateLimitMaxRequests, 10);
    QCOMPARE(config.pairs.at(0).watchLocalChanges, true);
    QCOMPARE(config.pairs.at(0).syncDeletions, false);
    QCOMPARE(config.pairs.at(0).syncMode, SyncMode::Bidirectional);
    QVERIFY(config.pairs.at(0).includePaths.isEmpty());
    QCOMPARE(config.pairs.at(0).trashDeletedFiles, true);
    QCOMPARE(config.pairs.at(0).trashRetentionDays, 30);
    QCOMPARE(config.settings.language, QStringLiteral("system"));
    QCOMPARE(config.settings.syncOnNetworkRecovery, true);
    QCOMPARE(config.settings.proxyMode, QStringLiteral("system"));
    QCOMPARE(config.settings.proxyPort, 8080);
    QCOMPARE(config.settings.logLevel, QStringLiteral("info"));
    QCOMPARE(config.settings.logMaxSizeMb, 2);
    QCOMPARE(config.settings.logRetentionFiles, 3);
    QCOMPARE(config.settings.retryCount, 2);
    QCOMPARE(config.settings.transferLimitKbps, 0);
    QCOMPARE(config.settings.retryDelaySeconds, 30);

    QVERIFY2(store.save(config, &error), qPrintable(error));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray saved = file.readAll();
    QVERIFY(!saved.contains("secret"));
    QJsonParseError parseError{};
    const QJsonObject root = QJsonDocument::fromJson(saved, &parseError).object();
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QCOMPARE(root.value(QStringLiteral("pairs")).toArray().size(), 1);

    QVERIFY2(store.save(config, &error), qPrintable(error));
    QVERIFY(QFileInfo::exists(path + QStringLiteral(".bak")));

    QFile invalid(path);
    QVERIFY(invalid.open(QIODevice::WriteOnly | QIODevice::Truncate));
    invalid.write(R"({"version": 99})");
    invalid.close();
    AppConfig future;
    QVERIFY(!store.load(&future, &error));
}

void TestConfigStore::roundTripsPairSettings()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("config.json"));
    ConfigStore store(path);

    AppConfig config;
    config.settings.notifications = false;
    config.settings.closeToTray = false;
    config.settings.autoSyncOnStartup = false;
    config.settings.startMinimized = true;
    config.settings.syncOnNetworkRecovery = false;
    config.settings.language = QStringLiteral("en");
    config.settings.proxyMode = QStringLiteral("manual");
    config.settings.proxyHost = QStringLiteral("proxy.example.com");
    config.settings.proxyPort = 3128;
    config.settings.proxyUser = QStringLiteral("proxy-user");
    config.settings.caBundlePath = QStringLiteral("/etc/ssl/custom.pem");
    config.settings.logLevel = QStringLiteral("warning");
    config.settings.logMaxSizeMb = 5;
    config.settings.logRetentionFiles = 7;
    config.settings.retryCount = 3;
    config.settings.retryDelaySeconds = 45;

    SyncPair pair;
    pair.id = QStringLiteral("pair-1");
    pair.name = QStringLiteral("Documents");
    pair.localPath = QStringLiteral("/home/user/Documents");
    pair.remoteUrl = QStringLiteral("https://example.com/dav/docs/");
    pair.username = QStringLiteral("user");
    pair.rememberPassword = true;
    pair.excludePatterns = {QStringLiteral(".git"), QStringLiteral("*.tmp")};
    pair.includePaths = {QStringLiteral("docs"), QStringLiteral("projects/notes.md")};
    pair.syncMode = SyncMode::RemoteToLocal;
    pair.conflictPolicy = ConflictPolicy::PreferRemote;
    pair.intervalMinutes = 42;
    pair.rateLimitEnabled = true;
    pair.rateLimitWindowMinutes = 120;
    pair.rateLimitMaxRequests = 7;
    pair.watchLocalChanges = false;
    pair.syncDeletions = true;
    pair.trashDeletedFiles = false;
    pair.trashRetentionDays = 90;
    pair.enabled = false;
    pair.syncOnStartup = true;
    pair.lastStatus = QStringLiteral("success");
    pair.lastMessage = QStringLiteral("done");
    pair.lastSync = QDateTime::currentDateTimeUtc();
    config.pairs.append(pair);

    QString error;
    QVERIFY2(store.save(config, &error), qPrintable(error));
    AppConfig loaded;
    QVERIFY2(store.load(&loaded, &error), qPrintable(error));
    QCOMPARE(loaded.settings.notifications, false);
    QCOMPARE(loaded.settings.closeToTray, false);
    QCOMPARE(loaded.settings.autoSyncOnStartup, false);
    QCOMPARE(loaded.settings.startMinimized, true);
    QCOMPARE(loaded.settings.syncOnNetworkRecovery, false);
    QCOMPARE(loaded.settings.language, QStringLiteral("en"));
    QCOMPARE(loaded.settings.proxyMode, QStringLiteral("manual"));
    QCOMPARE(loaded.settings.proxyHost, QStringLiteral("proxy.example.com"));
    QCOMPARE(loaded.settings.proxyPort, 3128);
    QCOMPARE(loaded.settings.proxyUser, QStringLiteral("proxy-user"));
    QCOMPARE(loaded.settings.caBundlePath, QStringLiteral("/etc/ssl/custom.pem"));
    QCOMPARE(loaded.settings.logLevel, QStringLiteral("warning"));
    QCOMPARE(loaded.settings.logMaxSizeMb, 5);
    QCOMPARE(loaded.settings.logRetentionFiles, 7);
    QCOMPARE(loaded.settings.retryCount, 3);
    QCOMPARE(loaded.settings.retryDelaySeconds, 45);
    QCOMPARE(loaded.pairs.size(), 1);
    const SyncPair &result = loaded.pairs.constFirst();
    QCOMPARE(result.id, pair.id);
    QCOMPARE(result.name, pair.name);
    QCOMPARE(result.localPath, pair.localPath);
    QCOMPARE(result.remoteUrl, pair.remoteUrl);
    QCOMPARE(result.username, pair.username);
    QCOMPARE(result.excludePatterns, pair.excludePatterns);
    QCOMPARE(result.includePaths, pair.includePaths);
    QCOMPARE(result.syncMode, SyncMode::RemoteToLocal);
    QCOMPARE(result.conflictPolicy, ConflictPolicy::PreferRemote);
    QCOMPARE(result.intervalMinutes, 42);
    QCOMPARE(result.rateLimitEnabled, true);
    QCOMPARE(result.rateLimitWindowMinutes, 120);
    QCOMPARE(result.rateLimitMaxRequests, 7);
    QCOMPARE(result.watchLocalChanges, false);
    QCOMPARE(result.syncDeletions, true);
    QCOMPARE(result.trashDeletedFiles, false);
    QCOMPARE(result.trashRetentionDays, 90);
    QCOMPARE(result.enabled, false);
    QVERIFY(result.lastSync.isValid());
}

} // namespace NutSync::Test
