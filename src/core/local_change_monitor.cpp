#include "core/local_change_monitor.h"

#include "core/path_utils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSocketNotifier>
#include <QTimer>

#include <utility>

#ifdef Q_OS_LINUX
#include <cerrno>
#include <cstring>
#include <sys/inotify.h>
#include <unistd.h>
#endif

namespace NutSync {

namespace {
constexpr int kDebounceMs = 1500;

#ifdef Q_OS_LINUX
constexpr uint32_t kWatchMask = IN_CLOSE_WRITE | IN_MODIFY | IN_ATTRIB | IN_CREATE | IN_DELETE
    | IN_DELETE_SELF | IN_MOVED_FROM | IN_MOVED_TO | IN_MOVE_SELF | IN_ONLYDIR | IN_DONT_FOLLOW;
#endif
} // namespace

LocalChangeMonitor::LocalChangeMonitor(QString rootPath, QStringList excludePatterns,
                                       QObject *parent)
    : QObject(parent)
    , m_rootPath(QDir::cleanPath(std::move(rootPath)))
    , m_excludePatterns(std::move(excludePatterns))
{
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, &LocalChangeMonitor::emitChange);
}

LocalChangeMonitor::~LocalChangeMonitor()
{
    stop();
}

QString LocalChangeMonitor::rootPath() const
{
    return m_rootPath;
}

QStringList LocalChangeMonitor::excludePatterns() const
{
    return m_excludePatterns;
}

int LocalChangeMonitor::watchCount() const
{
    return m_watchPaths.size();
}

bool LocalChangeMonitor::isActive() const
{
    return m_active;
}

bool LocalChangeMonitor::start(QString *error)
{
    stop();
    m_reportedErrors.clear();

#ifdef Q_OS_LINUX
    const QFileInfo rootInfo(m_rootPath);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        if (error)
            *error = tr("本地目录不存在，无法监视：%1").arg(m_rootPath);
        return false;
    }
    const QString canonical = rootInfo.canonicalFilePath();
    if (!canonical.isEmpty())
        m_rootPath = QDir::cleanPath(canonical);

    m_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (m_fd < 0) {
        if (error)
            *error = tr("无法初始化 inotify：%1").arg(QString::fromLocal8Bit(strerror(errno)));
        return false;
    }

    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &LocalChangeMonitor::readEvents);

    QString pathError;
    if (!addDirectory(m_rootPath, QString(), &pathError, true)) {
        if (error)
            *error = pathError;
        stop();
        return false;
    }
    m_active = true;
    return true;
#else
    if (error)
        *error = tr("当前平台不支持 inotify，本地变更监视不可用");
    return false;
#endif
}

void LocalChangeMonitor::stop()
{
#ifdef Q_OS_LINUX
    if (m_notifier) {
        m_notifier->setEnabled(false);
        delete m_notifier;
        m_notifier = nullptr;
    }
    if (m_fd >= 0) {
        for (auto it = m_watchPaths.cbegin(); it != m_watchPaths.cend(); ++it)
            inotify_rm_watch(m_fd, it.key());
        close(m_fd);
        m_fd = -1;
    }
#endif
    m_watchPaths.clear();
    m_watchedDirectories.clear();
    if (m_debounce)
        m_debounce->stop();
    m_dirty = false;
    m_active = false;
}

void LocalChangeMonitor::setSuppressed(bool suppressed)
{
    if (m_suppressed == suppressed)
        return;
    m_suppressed = suppressed;
    if (suppressed) {
        m_debounce->stop();
        m_dirty = false;
    }
}

void LocalChangeMonitor::refreshWatches()
{
#ifdef Q_OS_LINUX
    if (!m_active || m_fd < 0)
        return;
    QString error;
    if (!addDirectory(m_rootPath, QString(), &error, true)) {
        if (!error.isEmpty())
            reportError(error);
    }
#else
    Q_UNUSED(m_excludePatterns)
#endif
}

bool LocalChangeMonitor::addDirectory(const QString &absolutePath, const QString &relativePath,
                                      QString *error, bool isRoot)
{
#ifdef Q_OS_LINUX
    const QString cleaned = QDir::cleanPath(absolutePath);
    if (!relativePath.isEmpty() && PathUtils::isExcluded(relativePath, m_excludePatterns)) {
        return true;
    }

    if (!m_watchedDirectories.contains(cleaned)) {
        const int watch =
            inotify_add_watch(m_fd, QFile::encodeName(cleaned).constData(), kWatchMask);
        if (watch < 0) {
            const QString message =
                tr("无法监视目录“%1”：%2").arg(cleaned, QString::fromLocal8Bit(strerror(errno)));
            if (isRoot) {
                if (error)
                    *error = message;
                return false;
            }
            reportError(message);
            return true;
        }
        m_watchPaths.insert(watch, cleaned);
        m_watchedDirectories.insert(cleaned);
    }

    QDir directory(cleaned);
    if (!directory.isReadable())
        return true;
    const QFileInfoList children = directory.entryInfoList(
        QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name);
    for (const QFileInfo &child : children) {
        if (child.isSymLink() || !child.isDir())
            continue;
        const QString childRelative = relativePath.isEmpty()
            ? child.fileName()
            : relativePath + QLatin1Char('/') + child.fileName();
        if (PathUtils::isExcluded(childRelative, m_excludePatterns))
            continue;
        addDirectory(child.absoluteFilePath(), childRelative, nullptr, false);
    }
    return true;
#else
    Q_UNUSED(absolutePath)
    Q_UNUSED(relativePath)
    Q_UNUSED(isRoot)
    if (error)
        *error = tr("当前平台不支持 inotify");
    return false;
#endif
}

QString LocalChangeMonitor::relativePathFor(const QString &absolutePath) const
{
    const QString relative = QDir(m_rootPath).relativeFilePath(absolutePath);
    if (relative == QLatin1String("."))
        return {};
    if (relative.startsWith(QLatin1String("../")))
        return {};
    return relative;
}

void LocalChangeMonitor::removeWatchesUnder(const QString &absolutePrefix)
{
#ifdef Q_OS_LINUX
    const QString prefix = QDir::cleanPath(absolutePrefix);
    QList<int> toRemove;
    for (auto it = m_watchPaths.cbegin(); it != m_watchPaths.cend(); ++it) {
        if (it.value() == prefix || it.value().startsWith(prefix + QLatin1Char('/')))
            toRemove.append(it.key());
    }
    for (int watch : std::as_const(toRemove)) {
        inotify_rm_watch(m_fd, watch);
        const QString path = m_watchPaths.take(watch);
        m_watchedDirectories.remove(path);
    }
#else
    Q_UNUSED(absolutePrefix)
#endif
}

void LocalChangeMonitor::reportError(const QString &message)
{
    if (m_reportedErrors.contains(message) || m_reportedErrors.size() >= 10)
        return;
    m_reportedErrors.insert(message);
    emit monitorError(message);
}

void LocalChangeMonitor::markDirty()
{
    if (m_suppressed)
        return;
    m_dirty = true;
    m_debounce->start();
}

void LocalChangeMonitor::emitChange()
{
    if (m_suppressed || !m_dirty)
        return;
    m_dirty = false;
    emit localChanged();
}

void LocalChangeMonitor::readEvents()
{
#ifdef Q_OS_LINUX
    if (m_fd < 0)
        return;

    alignas(inotify_event) char buffer[64 * 1024];
    for (;;) {
        const ssize_t length = read(m_fd, buffer, sizeof(buffer));
        if (length < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            if (errno == EINTR)
                continue;
            reportError(
                tr("读取 inotify 事件失败：%1").arg(QString::fromLocal8Bit(strerror(errno))));
            break;
        }
        if (length == 0)
            break;
        ssize_t offset = 0;
        while (offset < length) {
            const auto *event = reinterpret_cast<const inotify_event *>(buffer + offset);
            offset += static_cast<ssize_t>(sizeof(inotify_event) + event->len);

            if (event->mask & IN_Q_OVERFLOW) {
                markDirty();
                continue;
            }
            if (event->mask & IN_IGNORED) {
                const QString ignoredPath = m_watchPaths.take(event->wd);
                if (!ignoredPath.isEmpty())
                    m_watchedDirectories.remove(ignoredPath);
                continue;
            }

            const QString watchPath = m_watchPaths.value(event->wd);
            if (watchPath.isEmpty())
                continue;

            const QString name = event->len > 0 ? QFile::decodeName(event->name) : QString();
            const QString absolute =
                name.isEmpty() ? watchPath : watchPath + QLatin1Char('/') + name;
            const QString relative = relativePathFor(absolute);
            if (!relative.isEmpty() && PathUtils::isExcluded(relative, m_excludePatterns)) {
                continue;
            }

            const bool isDirectory = event->mask & IN_ISDIR;
            if (isDirectory && (event->mask & (IN_CREATE | IN_MOVED_TO))) {
                addDirectory(absolute, relative, nullptr, false);
            }
            if (isDirectory
                && (event->mask & (IN_DELETE | IN_MOVED_FROM | IN_DELETE_SELF | IN_MOVE_SELF))) {
                removeWatchesUnder(absolute);
            }

            if (!m_suppressed)
                markDirty();
        }
    }
#endif
}

} // namespace NutSync
