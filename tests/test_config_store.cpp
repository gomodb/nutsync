#include "test_config_store.h"

#include "core/config_store.h"

#include <QFile>
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

    QVERIFY2(store.save(config, &error), qPrintable(error));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray saved = file.readAll();
    QVERIFY(!saved.contains("secret"));
    QJsonParseError parseError {};
    const QJsonObject root = QJsonDocument::fromJson(saved, &parseError).object();
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QCOMPARE(root.value(QStringLiteral("pairs")).toArray().size(), 1);
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

    SyncPair pair;
    pair.id = QStringLiteral("pair-1");
    pair.name = QStringLiteral("Documents");
    pair.localPath = QStringLiteral("/home/user/Documents");
    pair.remoteUrl = QStringLiteral("https://example.com/dav/docs/");
    pair.username = QStringLiteral("user");
    pair.rememberPassword = true;
    pair.excludePatterns = {QStringLiteral(".git"), QStringLiteral("*.tmp")};
    pair.conflictPolicy = ConflictPolicy::PreferRemote;
    pair.intervalMinutes = 42;
    pair.rateLimitEnabled = true;
    pair.rateLimitWindowMinutes = 120;
    pair.rateLimitMaxRequests = 7;
    pair.watchLocalChanges = false;
    pair.syncDeletions = true;
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
    QCOMPARE(loaded.pairs.size(), 1);
    const SyncPair &result = loaded.pairs.constFirst();
    QCOMPARE(result.id, pair.id);
    QCOMPARE(result.name, pair.name);
    QCOMPARE(result.localPath, pair.localPath);
    QCOMPARE(result.remoteUrl, pair.remoteUrl);
    QCOMPARE(result.username, pair.username);
    QCOMPARE(result.excludePatterns, pair.excludePatterns);
    QCOMPARE(result.conflictPolicy, ConflictPolicy::PreferRemote);
    QCOMPARE(result.intervalMinutes, 42);
    QCOMPARE(result.rateLimitEnabled, true);
    QCOMPARE(result.rateLimitWindowMinutes, 120);
    QCOMPARE(result.rateLimitMaxRequests, 7);
    QCOMPARE(result.watchLocalChanges, false);
    QCOMPARE(result.syncDeletions, true);
    QCOMPARE(result.enabled, false);
    QVERIFY(result.lastSync.isValid());
}

} // namespace NutSync::Test
