#include "ui/tray_controller.h"

#include "ui/app_icons.h"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>

#ifdef NUTSYNC_HAVE_KSTATUSNOTIFIER
#include <KStatusNotifierItem>
#endif
#ifdef NUTSYNC_HAVE_KNOTIFICATIONS
#include <KNotification>
#endif

namespace NutSync {

TrayController::TrayController(bool notificationsEnabled, QObject *parent)
    : QObject(parent)
    , m_notificationsEnabled(notificationsEnabled)
{
    m_menu = new QMenu();
    m_showAction =
        m_menu->addAction(QIcon::fromTheme(QStringLiteral("window-restore")), tr("显示 NutSync"));
    m_syncAction =
        m_menu->addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("立即同步全部"));
    m_menu->addSeparator();
    m_pauseAction = m_menu->addAction(tr("暂停自动同步"));
    m_pauseAction->setCheckable(true);
    m_menu->addSeparator();
    m_quitAction =
        m_menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")), tr("退出"));

    connect(m_showAction, &QAction::triggered, this, &TrayController::showRequested);
    connect(m_syncAction, &QAction::triggered, this, &TrayController::syncAllRequested);
    connect(m_pauseAction, &QAction::toggled, this, &TrayController::autoSyncToggled);
    connect(m_quitAction, &QAction::triggered, this, &TrayController::quitRequested);

#ifdef NUTSYNC_HAVE_KSTATUSNOTIFIER
    m_item = new KStatusNotifierItem(QStringLiteral("nutsync"), this);
    m_item->setCategory(KStatusNotifierItem::ApplicationStatus);
    m_item->setStatus(KStatusNotifierItem::Active);
    m_item->setTitle(QStringLiteral("NutSync"));
    m_item->setIconByPixmap(currentIcon());
    m_item->setToolTip(QStringLiteral("nutsync"), QStringLiteral("NutSync"), tr("WebDAV 双向同步"));
    m_item->setContextMenu(m_menu);
    m_item->setStandardActionsEnabled(false);
    connect(m_item, &KStatusNotifierItem::activateRequested, this, [this](bool, const QPoint &) {
        emit showRequested();
    });
    m_available = true;
#else
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_tray = new QSystemTrayIcon(currentIcon(), this);
        m_tray->setToolTip(QStringLiteral("NutSync"));
        m_tray->setContextMenu(m_menu);
        connect(m_tray, &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason reason) {
                    if (reason == QSystemTrayIcon::Trigger
                        || reason == QSystemTrayIcon::DoubleClick) {
                        emit showRequested();
                    }
                });
        m_tray->show();
        m_available = true;
    }
#endif
}

TrayController::~TrayController()
{
#ifdef NUTSYNC_HAVE_KSTATUSNOTIFIER
    if (m_item)
        m_item->setContextMenu(nullptr);
#else
    if (m_tray)
        m_tray->setContextMenu(nullptr);
#endif
    delete m_menu;
}

bool TrayController::isAvailable() const
{
    return m_available;
}

QIcon TrayController::currentIcon() const
{
    const bool dark = AppIcons::isDarkTheme();
    return m_busy ? AppIcons::syncing(dark) : AppIcons::monochrome(dark);
}

void TrayController::setStatus(const QString &title, const QString &subtitle,
                               const QString &iconName)
{
    if (!m_available)
        return;
    refreshTheme();
#ifdef NUTSYNC_HAVE_KSTATUSNOTIFIER
    m_item->setToolTip(iconName, title, subtitle);
    m_item->setTitle(title);
#else
    Q_UNUSED(iconName)
    m_tray->setToolTip(QStringLiteral("%1\n%2").arg(title, subtitle));
#endif
}

void TrayController::setBusy(bool busy)
{
    m_busy = busy;
    if (m_syncAction)
        m_syncAction->setEnabled(!busy);
    setStatus(QStringLiteral("NutSync"), busy ? tr("正在同步…") : tr("空闲"),
              busy ? QStringLiteral("emblem-synchronizing") : QStringLiteral("nutsync"));
}

void TrayController::refreshTheme()
{
    if (!m_available)
        return;
#ifdef NUTSYNC_HAVE_KSTATUSNOTIFIER
    m_item->setIconByPixmap(currentIcon());
#else
    if (m_tray)
        m_tray->setIcon(currentIcon());
#endif
}

void TrayController::setAutoSyncPaused(bool paused)
{
    if (!m_pauseAction)
        return;
    const QSignalBlocker blocker(m_pauseAction);
    m_pauseAction->setChecked(paused);
}

void TrayController::setNotificationsEnabled(bool enabled)
{
    m_notificationsEnabled = enabled;
}

void TrayController::showNotification(const QString &title, const QString &text, bool error)
{
    if (!m_notificationsEnabled)
        return;
#ifdef NUTSYNC_HAVE_KNOTIFICATIONS
    KNotification::event(
        error ? QStringLiteral("nutsync-error") : QStringLiteral("nutsync-sync"), title, text,
        error ? QStringLiteral("dialog-error") : QStringLiteral("dialog-information"),
        KNotification::CloseOnTimeout, QStringLiteral("nutsync"));
#else
#ifdef NUTSYNC_HAVE_KSTATUSNOTIFIER
    Q_UNUSED(error)
    Q_UNUSED(title)
    Q_UNUSED(text)
#else
    if (m_tray) {
        m_tray->showMessage(title, text,
                            error ? QSystemTrayIcon::Critical : QSystemTrayIcon::Information, 5000);
    }
#endif
#endif
}

} // namespace NutSync
