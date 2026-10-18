#include "core/logger.h"

#include "core/app_paths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include <cstdio>

namespace NutSync {

namespace {
QMutex g_mutex;
bool g_installed = false;
QString g_level = QStringLiteral("info");

int levelRank(const QString &level)
{
    if (level == QLatin1String("debug"))
        return 0;
    if (level == QLatin1String("warning"))
        return 2;
    if (level == QLatin1String("error"))
        return 3;
    return 1;
}

bool shouldLog(int messageRank)
{
    QMutexLocker locker(&g_mutex);
    return messageRank >= levelRank(g_level);
}

void writeLine(const QString &line)
{
    QMutexLocker locker(&g_mutex);
    QFile file(AppPaths::logFile());
    if (file.size() > 2 * 1024 * 1024) {
        file.close();
        const QString old = AppPaths::logFile() + QStringLiteral(".1");
        QFile::remove(old);
        QFile::rename(AppPaths::logFile(), old);
    }
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << line << '\n';
    }
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const char *level = "INFO";
    switch (type) {
    case QtDebugMsg:
        level = "DEBUG";
        break;
    case QtInfoMsg:
        level = "INFO";
        break;
    case QtWarningMsg:
        level = "WARN";
        break;
    case QtCriticalMsg:
        level = "ERROR";
        break;
    case QtFatalMsg:
        level = "FATAL";
        break;
    }

    const QString category = context.category ? QString::fromLatin1(context.category)
                                               : QStringLiteral("default");
    const QString line = QStringLiteral("%1 [%2] %3: %4")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                  QString::fromLatin1(level), category, message);
    const int messageRank = type == QtDebugMsg ? 0
        : type == QtInfoMsg ? 1
        : type == QtWarningMsg ? 2
        : 3;
    if (!shouldLog(messageRank)) {
        if (type == QtFatalMsg)
            std::abort();
        return;
    }
    std::fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
    writeLine(line);
    if (type == QtFatalMsg)
        std::abort();
}
} // namespace

void Logger::install()
{
    if (g_installed)
        return;
    AppPaths::ensureDirectories();
    qInstallMessageHandler(messageHandler);
    g_installed = true;
    qInfo() << "NutSync started, log file:" << AppPaths::logFile();
}

void Logger::setLevel(const QString &level)
{
    QMutexLocker locker(&g_mutex);
    g_level = (level == QLatin1String("debug") || level == QLatin1String("warning")
               || level == QLatin1String("error"))
        ? level
        : QStringLiteral("info");
}

QString Logger::level()
{
    QMutexLocker locker(&g_mutex);
    return g_level;
}

void Logger::log(const QString &category, const QString &message)
{
    if (!shouldLog(1))
        return;
    writeLine(QStringLiteral("%1 [UI] %2: %3")
                  .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), category,
                       message));
}

QString Logger::logFilePath()
{
    return AppPaths::logFile();
}

} // namespace NutSync
