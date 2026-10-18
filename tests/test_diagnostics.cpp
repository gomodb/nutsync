#include "test_diagnostics.h"

#include "core/diagnostics.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace NutSync::Test {

void TestDiagnostics::redactsSecrets()
{
    AppConfig config;
    SyncPair pair;
    pair.id = QStringLiteral("p");
    pair.name = QStringLiteral("Docs");
    pair.localPath = QStringLiteral("/home/user/Documents");
    pair.remoteUrl = QStringLiteral("https://alice:secret@example.com/dav/docs/");
    pair.username = QStringLiteral("alice");
    pair.password = QStringLiteral("secret");
    config.pairs.append(pair);

    const QString report = NutSync::Diagnostics::buildReport(config);
    QVERIFY(!report.contains(QStringLiteral("secret")));
    QVERIFY(!report.contains(QStringLiteral("alice")));
    QVERIFY(report.contains(QStringLiteral("example.com")));
    QVERIFY(report.contains(QStringLiteral("<path>")));
    QVERIFY(report.contains(QStringLiteral("username=<set>")));
}

void TestDiagnostics::readsLogTail()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("log.txt"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    for (int i = 0; i < 300; ++i)
        file.write(QStringLiteral("line %1\n").arg(i).toUtf8());
    file.close();

    const QString tail = NutSync::Diagnostics::readLogTail(path, 5);
    QCOMPARE(tail.count(QLatin1Char('\n')), 4);
    QVERIFY(tail.contains(QStringLiteral("line 299")));
    QVERIFY(!tail.contains(QStringLiteral("line 294")));
}

} // namespace NutSync::Test
