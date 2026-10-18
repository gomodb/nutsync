#include "core/app_paths.h"

#include <QDir>
#include <QStandardPaths>

namespace NutSync::AppPaths {

namespace {
QString xdgDir(QStandardPaths::StandardLocation location, const QString &fallbackSuffix)
{
    QString base = QStandardPaths::writableLocation(location);
    if (base.isEmpty())
        base = QDir::homePath() + fallbackSuffix;
    return base + QStringLiteral("/nutsync");
}
} // namespace

QString configDir()
{
    return xdgDir(QStandardPaths::GenericConfigLocation, QStringLiteral("/.config"));
}

QString configFile()
{
    return configDir() + QStringLiteral("/config.json");
}

QString dataDir()
{
    return xdgDir(QStandardPaths::GenericDataLocation, QStringLiteral("/.local/share"));
}

QString stateDatabase()
{
    return dataDir() + QStringLiteral("/state.db");
}

QString logFile()
{
    return dataDir() + QStringLiteral("/nutsync.log");
}

QString cacheDir()
{
    return xdgDir(QStandardPaths::GenericCacheLocation, QStringLiteral("/.cache"));
}

QString trashDir()
{
    return dataDir() + QStringLiteral("/trash");
}

void ensureDirectories()
{
    QDir().mkpath(configDir());
    QDir().mkpath(dataDir());
    QDir().mkpath(cacheDir());
}

} // namespace NutSync::AppPaths
