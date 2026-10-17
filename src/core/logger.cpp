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
    std::fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
    if (type != QtDebugMsg)
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

void Logger::log(const QString &category, const QString &message)
{
    writeLine(QStringLiteral("%1 [UI] %2: %3")
                  .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), category,
                       message));
}

QString Logger::logFilePath()
{
    return AppPaths::logFile();
}

} // namespace NutSync
