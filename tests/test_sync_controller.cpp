#include "test_sync_controller.h"

#include "core/sync_types.h"
#include "ui/sync_controller.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>

namespace NutSync::Test {

namespace {

class Server
{
public:
    explicit Server(const QString &root)
    {
        m_process.start(QStringLiteral("python3"),
                        {QStringLiteral(NUTSYNC_WEBDAV_SERVER), QStringLiteral("--root"), root,
                         QStringLiteral("--user"), QStringLiteral("tester"),
                         QStringLiteral("--password"), QStringLiteral("s3cret")});
        if (!m_process.waitForStarted(5000))
            return;
        QByteArray output;
        QElapsedTimer timer;
        timer.start();
        while (!output.contains("PORT=") && timer.elapsed() < 8000
               && m_process.state() != QProcess::NotRunning) {
            m_process.waitForReadyRead(200);
            output += m_process.readAllStandardOutput();
        }
        const int index = output.indexOf("PORT=");
        if (index >= 0) {
            const QByteArray tail = output.mid(index + 5);
            m_port = tail.left(tail.indexOf('\n')).trimmed().toInt();
        }
    }

    ~Server()
    {
        if (m_process.state() != QProcess::NotRunning) {
            m_process.terminate();
            if (!m_process.waitForFinished(1500)) {
                m_process.kill();
                m_process.waitForFinished(1000);
            }
        }
    }

    bool valid() const { return m_port > 0; }
    int port() const { return m_port; }

private:
    QProcess m_process;
    int m_port = 0;
};

} // namespace

void TestSyncController::queuedSyncRunsInWorkerAndReportsSummary()
{
    QTemporaryDir remoteDir;
    QTemporaryDir localDir;
    QTemporaryDir stateDir;
    QVERIFY(remoteDir.isValid() && localDir.isValid() && stateDir.isValid());

    Server server(remoteDir.path());
    QVERIFY(server.valid());

    const QString localFile = QDir(localDir.path()).filePath(QStringLiteral("queued.txt"));
    QFile file(localFile);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("queued content");
    file.close();

    SyncPair pair;
    pair.id = QStringLiteral("controller-test");
    pair.name = QStringLiteral("Controller Test");
    pair.localPath = localDir.path();
    pair.remoteUrl = QStringLiteral("http://127.0.0.1:%1/dav/").arg(server.port());
    pair.username = QStringLiteral("tester");
    pair.password = QStringLiteral("s3cret");
    pair.rememberPassword = false;
    pair.enabled = true;
    pair.intervalMinutes = 0;

    qRegisterMetaType<NutSync::SyncSummary>("NutSync::SyncSummary");
    SyncController controller(stateDir.filePath(QStringLiteral("state.db")));
    QSignalSpy finishedSpy(&controller, &SyncController::pairFinished);

    controller.enqueue(pair);
    QVERIFY(controller.isBusy() || controller.pendingCount() == 1);
    QVERIFY(finishedSpy.wait(20000));
    QCOMPARE(finishedSpy.count(), 1);

    const SyncSummary summary =
        qvariant_cast<SyncSummary>(finishedSpy.constFirst().constFirst());
    QVERIFY2(summary.success, qPrintable(summary.message + "\n"
                                         + summary.errorDetails.join('\n')));
    QCOMPARE(summary.uploaded, 1);
    QVERIFY(QFileInfo::exists(QDir(remoteDir.path()).filePath(QStringLiteral("queued.txt"))));

    QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 5000);
    QCOMPARE(controller.pendingCount(), 0);
}

} // namespace NutSync::Test
