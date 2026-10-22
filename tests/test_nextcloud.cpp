#include "test_nextcloud.h"

#include "core/webdav_backend.h"

#include <QFile>
#include <algorithm>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

namespace NutSync::Test {

void TestNextcloud::webdavRoundTrip()
{
    const QString baseUrl = qEnvironmentVariable("NUTSYNC_NEXTCLOUD_URL");
    if (baseUrl.isEmpty())
        QSKIP("NUTSYNC_NEXTCLOUD_URL is not configured");

    WebDavOptions options;
    options.baseUrl = QUrl(baseUrl);
    options.username = qEnvironmentVariable("NUTSYNC_NEXTCLOUD_USER");
    options.password = qEnvironmentVariable("NUTSYNC_NEXTCLOUD_PASSWORD");
    options.timeoutMs = 30000;
    options.retryCount = 2;
    options.retryBaseDelayMs = 200;
    options.retryMaxDelayMs = 2000;

    WebDavBackend backend(options);
    QString error;
    QVERIFY2(backend.testConnection(&error), qPrintable(error));

    const QString directory =
        QStringLiteral("nutsync-integration-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVERIFY2(backend.makeDirectory(directory, &error), qPrintable(error));

    QTemporaryDir local;
    QVERIFY(local.isValid());
    const QString source = local.filePath(QStringLiteral("source.txt"));
    QFile sourceFile(source);
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    const QByteArray content("nextcloud integration");
    QCOMPARE(sourceFile.write(content), qint64(content.size()));
    sourceFile.close();

    const QString remotePath = directory + QStringLiteral("/source.txt");
    QVERIFY2(backend.upload(source, remotePath, QStringLiteral("*"), &error, {}),
             qPrintable(error));
    const QString destination = local.filePath(QStringLiteral("destination.txt"));
    DownloadResult result;
    QVERIFY2(backend.download(remotePath, destination, &result, &error, {}), qPrintable(error));
    QFile downloaded(destination);
    QVERIFY(downloaded.open(QIODevice::ReadOnly));
    QCOMPARE(downloaded.readAll(), QByteArray("nextcloud integration"));

    QList<RemoteEntry> entries;
    QVERIFY2(backend.scan(&entries, &error, {}), qPrintable(error));
    QVERIFY(std::any_of(entries.cbegin(), entries.cend(), [&remotePath](const RemoteEntry &entry) {
        return entry.path == remotePath;
    }));
    QVERIFY2(backend.remove(directory, true, {}, &error, {}), qPrintable(error));
}

} // namespace NutSync::Test
