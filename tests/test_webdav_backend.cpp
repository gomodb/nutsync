#include "test_webdav_backend.h"

#include "core/path_utils.h"
#include "core/sync_engine.h"
#include "core/webdav_backend.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTest>
#include <QTemporaryDir>

namespace NutSync::Test {

namespace {

class ServerProcess
{
public:
    ServerProcess(const QString &root)
    {
        m_process.setProcessChannelMode(QProcess::SeparateChannels);
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
        m_process.setProcessEnvironment(environment);
        m_process.start(QStringLiteral("python3"),
                        {QStringLiteral(NUTSYNC_WEBDAV_SERVER), QStringLiteral("--root"), root,
                         QStringLiteral("--user"), QStringLiteral("tester"),
                         QStringLiteral("--password"), QStringLiteral("s3cret"), QStringLiteral("--verbose")});
        if (!m_process.waitForStarted(5000))
            return;

        QByteArray output;
        QElapsedTimer timer;
        timer.start();
        while (!output.contains("PORT=") && timer.elapsed() < 8000) {
            if (m_process.state() == QProcess::NotRunning)
                break;
            m_process.waitForReadyRead(200);
            output += m_process.readAllStandardOutput();
            if (output.contains("PORT="))
                break;
        }
        const int portIndex = output.indexOf("PORT=");
        if (portIndex >= 0) {
            const QByteArray tail = output.mid(portIndex + 5);
            m_port = tail.left(tail.indexOf('\n') < 0 ? tail.size() : tail.indexOf('\n'))
                         .trimmed()
                         .toInt();
        }
    }

    ~ServerProcess()
    {
        if (m_process.state() != QProcess::NotRunning) {
            m_process.terminate();
            if (!m_process.waitForFinished(1500)) {
                m_process.kill();
                m_process.waitForFinished(1000);
            }
        }
    }

    bool isValid() const { return m_port > 0; }
    int port() const { return m_port; }
    QByteArray stderrText() { return m_process.readAllStandardError(); }

private:
    QProcess m_process;
    int m_port = 0;
};

QString writeLocalFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    file.write(content);
    file.close();
    return path;
}

WebDavOptions optionsFor(int port, const QString &user = QStringLiteral("tester"),
                         const QString &password = QStringLiteral("s3cret"))
{
    WebDavOptions options;
    options.baseUrl = QUrl(QStringLiteral("http://127.0.0.1:%1/dav/").arg(port));
    options.username = user;
    options.password = password;
    options.timeoutMs = 15000;
    return options;
}

} // namespace

void TestWebDavBackend::backendOperations()
{
    QTemporaryDir remoteDir;
    QVERIFY(remoteDir.isValid());
    ServerProcess server(remoteDir.path());
    QVERIFY2(server.isValid(), qPrintable(QString::fromUtf8(server.stderrText())));
    const QString baseUrl = QStringLiteral("http://127.0.0.1:%1/dav/").arg(server.port());

    {
        WebDavBackend wrongAuth(optionsFor(server.port(), QStringLiteral("tester"),
                                           QStringLiteral("wrong")));
        QString error;
        QVERIFY(!wrongAuth.testConnection(&error));
        QVERIFY2(error.contains(QStringLiteral("401")), qPrintable(error));
    }

    WebDavBackend backend(optionsFor(server.port()));
    QString error;
    QVERIFY2(backend.testConnection(&error), qPrintable(error));
    QVERIFY2(backend.makeDirectory(QStringLiteral("sub"), &error), qPrintable(error));

    QTemporaryDir localDir;
    QVERIFY(localDir.isValid());
    const QString source = writeLocalFile(
        localDir.filePath(QStringLiteral("hello world-你好.txt")), QByteArray("hello webdav"));
    QVERIFY(!source.isEmpty());

    QVERIFY2(backend.upload(source, QStringLiteral("sub/hello world-你好.txt"), QStringLiteral("*"),
                            &error, {}),
             qPrintable(error));

    RemoteEntry entry;
    bool exists = false;
    QVERIFY2(backend.stat(QStringLiteral("sub/hello world-你好.txt"), &entry, &exists, &error),
             qPrintable(error));
    QVERIFY(exists);
    QCOMPARE(entry.type, EntryType::File);
    QCOMPARE(entry.size, qint64(12));
    QVERIFY(!entry.etag.isEmpty());

    QList<RemoteEntry> entries;
    QVERIFY2(backend.scan(&entries, &error, {}), qPrintable(error));
    bool foundDirectory = false;
    bool foundFile = false;
    for (const RemoteEntry &remote : std::as_const(entries)) {
        if (remote.path == QStringLiteral("sub") && remote.type == EntryType::Directory)
            foundDirectory = true;
        if (remote.path == QStringLiteral("sub/hello world-你好.txt")
            && remote.type == EntryType::File) {
            foundFile = true;
        }
    }
    QVERIFY(foundDirectory);
    QVERIFY(foundFile);

    QString secondUploadError;
    QVERIFY(!backend.upload(source, QStringLiteral("sub/hello world-你好.txt"), QStringLiteral("*"),
                            &secondUploadError, {}));
    QVERIFY(secondUploadError.contains(QStringLiteral("412")));

    const QString downloaded = localDir.filePath(QStringLiteral("downloaded.txt"));
    DownloadResult downloadResult;
    const bool downloadedOk = backend.download(QStringLiteral("sub/hello world-你好.txt"),
                                               downloaded, &downloadResult, &error, {});
    QVERIFY2(downloadedOk, qPrintable(error + QLatin1Char('\n') + server.stderrText()));
    QFile downloadedFile(downloaded);
    QVERIFY(downloadedFile.open(QIODevice::ReadOnly));
    QCOMPARE(downloadedFile.readAll(), QByteArray("hello webdav"));
    QVERIFY(downloadResult.size > 0);

    QVERIFY2(backend.remove(QStringLiteral("sub/hello world-你好.txt"), false, entry.etag,
                            &error, {}),
             qPrintable(error));
    entry = {};
    QVERIFY2(backend.stat(QStringLiteral("sub/hello world-你好.txt"), &entry, &exists, &error),
             qPrintable(error));
    QVERIFY(!exists);

    QVERIFY2(backend.remove(QStringLiteral("sub"), true, {}, &error, {}), qPrintable(error));
    QVERIFY2(backend.testConnection(&error), qPrintable(error));
    Q_UNUSED(baseUrl)
}

void TestWebDavBackend::engineEndToEnd()
{
    QTemporaryDir remoteDir;
    QVERIFY(remoteDir.isValid());
    ServerProcess server(remoteDir.path());
    QVERIFY2(server.isValid(), qPrintable(QString::fromUtf8(server.stderrText())));

    QTemporaryDir localDir;
    QTemporaryDir stateDir;
    QVERIFY(localDir.isValid());
    QVERIFY(stateDir.isValid());
    const QString localRoot = localDir.path();
    QVERIFY(!writeLocalFile(QDir(localRoot).filePath(QStringLiteral("nested/文档.txt")),
                            QByteArray("first"))
                 .isEmpty());

    SyncPair pair;
    pair.id = QStringLiteral("webdav-e2e");
    pair.name = QStringLiteral("WebDAV E2E");
    pair.localPath = localRoot;
    pair.remoteUrl = QStringLiteral("http://127.0.0.1:%1/dav/").arg(server.port());
    pair.username = QStringLiteral("tester");
    pair.password = QStringLiteral("s3cret");
    pair.conflictPolicy = ConflictPolicy::KeepBoth;
    pair.enabled = true;
    pair.intervalMinutes = 0;

    const QString database = stateDir.filePath(QStringLiteral("state.db"));
    auto run = [&]() {
        SyncEngine engine(pair, database);
        SyncSummary result;
        QObject::connect(&engine, &SyncEngine::finished, &engine,
                         [&result](const SyncSummary &summary) { result = summary; });
        engine.run();
        return result;
    };

    SyncSummary summary = run();
    QVERIFY2(summary.success, qPrintable(summary.message + "\n"
                                         + summary.errorDetails.join('\n')));
    QCOMPARE(summary.uploaded, 1);
    QVERIFY(QFileInfo::exists(
        QDir(remoteDir.path()).filePath(QStringLiteral("nested/文档.txt"))));

    summary = run();
    QVERIFY2(summary.success, qPrintable(summary.message + "\n"
                                         + summary.errorDetails.join('\n')));
    QCOMPARE(summary.uploaded, 0);
    QCOMPARE(summary.downloaded, 0);

    QVERIFY(!writeLocalFile(QDir(localRoot).filePath(QStringLiteral("nested/文档.txt")),
                            QByteArray("second"))
                 .isEmpty());
    summary = run();
    QVERIFY2(summary.success, qPrintable(summary.message + "\n"
                                         + summary.errorDetails.join('\n')));
    QCOMPARE(summary.uploaded, 1);

    QFile remoteFile(QDir(remoteDir.path()).filePath(QStringLiteral("nested/文档.txt")));
    QVERIFY(remoteFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    remoteFile.write("remote change");
    remoteFile.close();

    summary = run();
    QVERIFY2(summary.success, qPrintable(summary.message + "\n"
                                         + summary.errorDetails.join('\n')));
    QCOMPARE(summary.downloaded, 1);
    QFile localFile(QDir(localRoot).filePath(QStringLiteral("nested/文档.txt")));
    QVERIFY(localFile.open(QIODevice::ReadOnly));
    QCOMPARE(localFile.readAll(), QByteArray("remote change"));
}

} // namespace NutSync::Test
