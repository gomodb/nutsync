#pragma once

#include <QIcon>
#include <QObject>
#include <QString>

class QAction;
class QMenu;
class QSystemTrayIcon;

#ifdef NUTSYNC_HAVE_KSTATUSNOTIFIER
class KStatusNotifierItem;
#endif

namespace NutSync {

class TrayController : public QObject
{
    Q_OBJECT

public:
    explicit TrayController(bool notificationsEnabled, QObject *parent = nullptr);
    ~TrayController() override;

    bool isAvailable() const;
    void setStatus(const QString &title, const QString &subtitle, const QString &iconName);
    void setBusy(bool busy);
    void refreshTheme();
    void setAutoSyncPaused(bool paused);
    void setNotificationsEnabled(bool enabled);
    void showNotification(const QString &title, const QString &text, bool error = false);

signals:
    void showRequested();
    void syncAllRequested();
    void quitRequested();
    void autoSyncToggled(bool paused);

private:
    QIcon currentIcon() const;

    QMenu *m_menu = nullptr;
    QAction *m_showAction = nullptr;
    QAction *m_syncAction = nullptr;
    QAction *m_pauseAction = nullptr;
    QAction *m_quitAction = nullptr;
    bool m_notificationsEnabled = true;
    bool m_available = false;
    bool m_busy = false;

#ifdef NUTSYNC_HAVE_KSTATUSNOTIFIER
    KStatusNotifierItem *m_item = nullptr;
#else
    QSystemTrayIcon *m_tray = nullptr;
#endif
};

} // namespace NutSync
