#include "test_autostart.h"

#include "core/autostart_manager.h"

#include <QFile>
#include <QFileInfo>
#include <QTest>
#include <QTemporaryDir>

namespace NutSync::Test {

void TestAutostart::disabledByDefault()
{
    QTemporaryDir configDir;
    QVERIFY(configDir.isValid());
    AutostartManager manager(configDir.path());
    QVERIFY(!manager.isEnabled());
    QVERIFY(!QFileInfo::exists(manager.desktopFilePath()));
    QCOMPARE(manager.desktopFilePath(),
             configDir.filePath(QStringLiteral("autostart/io.github.gomodb.NutSync.desktop")));
}

void TestAutostart::enablesAndDisablesDesktopEntry()
{
    QTemporaryDir configDir;
    QVERIFY(configDir.isValid());
    AutostartManager manager(configDir.path());
    QString error;

    QVERIFY2(manager.setEnabled(true, &error), qPrintable(error));
    QVERIFY(manager.isEnabled());
    QVERIFY2(manager.setEnabled(true, &error), qPrintable(error));

    QFile file(manager.desktopFilePath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray contents = file.readAll();
    file.close();
    QVERIFY(contents.contains("[Desktop Entry]"));
    QVERIFY(contents.contains("Type=Application"));
    QVERIFY(contents.contains("Exec="));
    QVERIFY(contents.contains("--hidden"));
    QVERIFY(contents.contains("X-KDE-autostart-after=panel"));
    QVERIFY(contents.contains("X-GNOME-Autostart-enabled=true"));

    QVERIFY2(manager.setEnabled(false, &error), qPrintable(error));
    QVERIFY(!manager.isEnabled());
    QVERIFY(!QFileInfo::exists(manager.desktopFilePath()));
    QVERIFY2(manager.setEnabled(false, &error), qPrintable(error));
}

void TestAutostart::migratesLegacyDesktopEntry()
{
    QTemporaryDir configDir;
    QVERIFY(configDir.isValid());
    const QString autostartDir = configDir.filePath(QStringLiteral("autostart"));
    QVERIFY(QDir().mkpath(autostartDir));
    const QString legacy = QDir(autostartDir)
        .filePath(QStringLiteral("io.github.nutsync.NutSync.desktop"));
    QFile file(legacy);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("[Desktop Entry]\n");
    file.close();

    AutostartManager manager(configDir.path());
    QVERIFY(manager.isEnabled());
    QVERIFY(!QFileInfo::exists(legacy));
    QCOMPARE(manager.desktopFilePath(), QDir(autostartDir)
        .filePath(QStringLiteral("io.github.gomodb.NutSync.desktop")));
}

} // namespace NutSync::Test
