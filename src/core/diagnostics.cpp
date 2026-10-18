#include "core/diagnostics.h"

#include "core/app_paths.h"
#include "core/logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSysInfo>
#include <QTextStream>
#include <QUrl>

namespace NutSync::Diagnostics {

namespace {
QString redactedLocalPath(const QString &path)
{
    const QString home = QDir::homePath();
    if (!home.isEmpty() && path.startsWith(home))
        return QStringLiteral("~") + path.mid(home.size());
    return path;
}

QString redactedRemoteUrl(const QString &value)
{
    const QUrl url(value);
    if (!url.isValid() || url.host().isEmpty())
        return QStringLiteral("<invalid>");
    return QStringLiteral("%1://%2:%3/<path>")
        .arg(url.scheme(), url.host())
        .arg(url.port(url.scheme() == QLatin1String("https") ? 443 : 80));
}

QString secretState(const QString &value)
{
    return value.isEmpty() ? QStringLiteral("<empty>") : QStringLiteral("<set>");
}
} // namespace

QString buildReport(const AppConfig &config)
{
    QString report;
    QTextStream out(&report);
    out << "NutSync diagnostics\n";
    out << "Generated: " << QDateTime::currentDateTime().toString(Qt::ISODate) << '\n';
    out << "NutSync: " << QString::fromLatin1(NUTSYNC_VERSION) << '\n';
    out << "Qt: " << QString::fromLatin1(qVersion()) << '\n';
    out << "OS: " << QSysInfo::prettyProductName() << '\n';
    out << "Kernel: " << QSysInfo::kernelType() << ' ' << QSysInfo::kernelVersion() << '\n';
    out << "Architecture: " << QSysInfo::currentCpuArchitecture() << '\n';
    out << "Locale: " << QLocale::system().name() << '\n';
    out << "Config: " << redactedLocalPath(AppPaths::configFile()) << '\n';
    out << "State database: " << redactedLocalPath(AppPaths::stateDatabase()) << '\n';
    out << "Log: " << redactedLocalPath(Logger::logFilePath()) << '\n';
    out << '\n';

    out << "Settings:\n";
    out << "  language=" << config.settings.language << '\n';
    out << "  proxyMode=" << config.settings.proxyMode << '\n';
    out << "  proxyHost=" << secretState(config.settings.proxyHost) << '\n';
    out << "  proxyUser=" << secretState(config.settings.proxyUser) << '\n';
    out << "  caBundle=" << secretState(config.settings.caBundlePath) << '\n';
    out << "  logLevel=" << config.settings.logLevel << '\n';
    out << "  logMaxSizeMb=" << config.settings.logMaxSizeMb << '\n';
    out << "  logRetentionFiles=" << config.settings.logRetentionFiles << '\n';
    out << "  retryCount=" << config.settings.retryCount << '\n';
    out << "  retryMaxSeconds=" << config.settings.retryDelaySeconds << '\n';
    out << "  transferLimitKbps=" << config.settings.transferLimitKbps << '\n';
    out << '\n';

    out << "Sync pairs: " << config.pairs.size() << '\n';
    for (const SyncPair &pair : config.pairs) {
        out << "  - name=" << pair.name << '\n';
        out << "    enabled=" << (pair.enabled ? "yes" : "no") << '\n';
        out << "    local=" << redactedLocalPath(pair.localPath) << '\n';
        out << "    remote=" << redactedRemoteUrl(pair.remoteUrl) << '\n';
        out << "    username=" << secretState(pair.username) << '\n';
        out << "    intervalMinutes=" << pair.intervalMinutes << '\n';
        out << "    conflictPolicy=" << conflictPolicyToString(pair.conflictPolicy) << '\n';
        out << "    syncDeletions=" << (pair.syncDeletions ? "yes" : "no") << '\n';
        out << "    trashDeletedFiles=" << (pair.trashDeletedFiles ? "yes" : "no") << '\n';
        out << "    trashRetentionDays=" << pair.trashRetentionDays << '\n';
        out << "    watchLocalChanges=" << (pair.watchLocalChanges ? "yes" : "no") << '\n';
        out << "    exclusions=" << pair.excludePatterns.size() << '\n';
    }
    out << "\nPasswords and remote/local paths beyond the redacted form are not included.\n";
    return report;
}

QString readLogTail(const QString &logPath, int maxLines)
{
    QFile file(logPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const qint64 size = file.size();
    const qint64 start = qMax<qint64>(0, size - 64 * 1024);
    if (!file.seek(start))
        return {};
    QString text = QString::fromUtf8(file.readAll());
    QStringList lines = text.split(QLatin1Char('\n'));
    if (start > 0 && !lines.isEmpty())
        lines.removeFirst();
    if (lines.size() > maxLines)
        lines = lines.mid(lines.size() - maxLines);
    return lines.join(QLatin1Char('\n'));
}

} // namespace NutSync::Diagnostics
