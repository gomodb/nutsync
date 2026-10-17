#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class QSocketNotifier;
class QTimer;

namespace NutSync {

/**
 * Recursive local file monitor backed by Linux inotify.
 *
 * One inotify watch is installed per directory. Created directories are added
 * automatically, and excluded subtrees are never watched. Events are debounced
 * before emitting localChanged(). While suppressed the monitor continues to
 * maintain its watches but does not emit change notifications, which prevents
 * NutSync's own downloads/deletes from causing a synchronization loop.
 */
class LocalChangeMonitor : public QObject
{
    Q_OBJECT

public:
    explicit LocalChangeMonitor(QString rootPath, QStringList excludePatterns,
                                QObject *parent = nullptr);
    ~LocalChangeMonitor() override;

    bool start(QString *error = nullptr);
    void stop();
    bool isActive() const;

    void setSuppressed(bool suppressed);
    void refreshWatches();

    QString rootPath() const;
    QStringList excludePatterns() const;
    int watchCount() const;

signals:
    void localChanged();
    void monitorError(const QString &message);

private:
    void readEvents();
    void emitChange();
    void markDirty();
    bool addDirectory(const QString &absolutePath, const QString &relativePath,
                      QString *error, bool isRoot);
    void removeWatchesUnder(const QString &absolutePrefix);
    QString relativePathFor(const QString &absolutePath) const;
    void reportError(const QString &message);

    QString m_rootPath;
    QStringList m_excludePatterns;
    int m_fd = -1;
    QSocketNotifier *m_notifier = nullptr;
    QTimer *m_debounce = nullptr;
    QHash<int, QString> m_watchPaths;
    QSet<QString> m_watchedDirectories;
    QSet<QString> m_reportedErrors;
    bool m_active = false;
    bool m_suppressed = false;
    bool m_dirty = false;
};

} // namespace NutSync
