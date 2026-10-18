#include "core/autostart_manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <utility>

namespace NutSync {

namespace {
constexpr auto kDesktopFileName = "io.github.gomodb.NutSync.desktop";
constexpr auto kLegacyDesktopFileName = "io.github.nutsync.NutSync.desktop";

QString quoteExecArgument(QString value)
{
    value.replace(QLatin1Char('%'), QLatin1String("%%"));
    const bool needsQuotes = std::any_of(value.cbegin(), value.cend(), [](QChar character) {
        return !(character.isLetterOrNumber() || character == QLatin1Char('/')
                 || character == QLatin1Char('_') || character == QLatin1Char('-')
                 || character == QLatin1Char('.') || character == QLatin1Char('+'));
    });
    if (!needsQuotes)
        return value;

    value.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    value.replace(QLatin1Char('"'), QLatin1String("\\\""));
    value.replace(QLatin1Char('`'), QLatin1String("\\`"));
    value.replace(QLatin1Char('$'), QLatin1String("\\$"));
    return QLatin1Char('"') + value + QLatin1Char('"');
}

QString applicationCommand()
{
    const QString applicationPath = QCoreApplication::applicationFilePath();
    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("nutsync"));
    if (!fromPath.isEmpty()
        && QFileInfo(fromPath).canonicalFilePath()
            == QFileInfo(applicationPath).canonicalFilePath()) {
        return QStringLiteral("nutsync");
    }
    return applicationPath;
}

QString desktopFileContents()
{
    const QString command = quoteExecArgument(applicationCommand());
    return QStringLiteral(
               "[Desktop Entry]\n"
               "Type=Application\n"
               "Version=1.0\n"
               "Name=NutSync\n"
               "Comment=Start NutSync WebDAV two-way synchronization in the system tray\n"
               "Exec=")
        + command
        + QStringLiteral(
              " --hidden\n"
              "Icon=nutsync\n"
              "Terminal=false\n"
              "StartupNotify=false\n"
              "Categories=Utility;Network;\n"
              "X-GNOME-Autostart-enabled=true\n"
              "X-KDE-autostart-after=panel\n"
              "X-KDE-autostart-phase=2\n"
              "Hidden=false\n");
}
} // namespace

AutostartManager::AutostartManager(QString configDir)
    : m_configDir(configDir.isEmpty() ? defaultConfigDir() : std::move(configDir))
{
    migrateLegacyEntry();
}

void AutostartManager::migrateLegacy()
{
    AutostartManager manager;
    Q_UNUSED(manager)
}

void AutostartManager::migrateLegacyEntry() const
{
    const QString current = desktopFilePath();
    const QString legacy = QDir(m_configDir).filePath(
        QStringLiteral("autostart/") + QString::fromLatin1(kLegacyDesktopFileName));
    QFile currentFile(current);
    if (currentFile.exists()) {
        QFile::remove(legacy);
        return;
    }

    QFile legacyFile(legacy);
    if (!legacyFile.exists())
        return;

    const QString directory = QFileInfo(current).absolutePath();
    if (!QDir().mkpath(directory))
        return;
    if (QFile::rename(legacy, current))
        return;
    if (legacyFile.copy(current)) {
        QFile::remove(legacy);
    }
}

QString AutostartManager::defaultConfigDir()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (path.isEmpty())
        path = QDir::homePath() + QStringLiteral("/.config");
    return path;
}

QString AutostartManager::desktopFileName()
{
    return QString::fromLatin1(kDesktopFileName);
}

QString AutostartManager::desktopFilePath() const
{
    return QDir(m_configDir).filePath(
        QStringLiteral("autostart/") + QString::fromLatin1(kDesktopFileName));
}

bool AutostartManager::isEnabled() const
{
    const QFileInfo info(desktopFilePath());
    return info.exists() && info.isFile() && info.size() > 0;
}

bool AutostartManager::setEnabled(bool enabled, QString *error) const
{
    const QString path = desktopFilePath();
    if (!enabled) {
        QFile file(path);
        if (file.exists() && !file.remove()) {
            if (error) {
                *error = QObject::tr("无法删除开机自启条目“%1”：%2")
                             .arg(path, file.errorString());
            }
            return false;
        }
        return true;
    }

    const QString directory = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directory)) {
        if (error)
            *error = QObject::tr("无法创建开机自启目录“%1”").arg(directory);
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QObject::tr("无法写入开机自启条目“%1”：%2")
                         .arg(path, file.errorString());
        }
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                        | QFileDevice::ReadGroup | QFileDevice::ReadOther);
    file.write(desktopFileContents().toUtf8());
    if (!file.commit()) {
        if (error) {
            *error = QObject::tr("保存开机自启条目失败：%1").arg(file.errorString());
        }
        return false;
    }
    return true;
}

} // namespace NutSync
