#pragma once

#include "core/config_store.h"
#include "core/credential_store.h"
#include "core/sync_rate_limiter.h"
#include "core/sync_types.h"

#include <QHash>
#include <QMainWindow>
#include <QSet>

class QAction;
class QFileSystemWatcher;
class QLabel;
class QProgressBar;
class QPlainTextEdit;
class QStackedWidget;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace NutSync {

class LocalChangeMonitor;
class SyncController;
class TrayController;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool shouldStartHidden() const;
    bool hasTray() const;

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void buildUi();
    void buildActions();
    void loadConfig();
    void saveConfig(bool showError);
    void setupTray();
    void refreshPairs(const QString &selectId = {});
    void updateDetails();
    void updateActions();
    void updatePairRow(const SyncPair &pair);
    void updateTrayIdleStatus();

    SyncPair *findPair(const QString &id);
    const SyncPair *findPair(const QString &id) const;
    QString selectedPairId() const;
    void selectPair(const QString &id);

    void addPair();
    void editPair();
    void removePair();
    void resetPairState();
    void editCurrentPair(const QString &id);
    void removePairById(const QString &id);
    void requestSync(const QString &id, bool manual = true);
    void requestSyncSelected();
    void requestSyncAll();
    void cancelSync();
    bool clearPairState(const QString &id, bool showError = true);

    void onPairStarted(const QString &pairId);
    void onLogMessage(const QString &pairId, const QString &message);
    void onProgress(const QString &pairId, int completed, int total, const QString &path);
    void onPairFinished(const SyncSummary &summary);
    void onBusyChanged(bool busy);
    void onQueueChanged(int pending);

    void scheduleTick();
    void syncStartupPairs();
    void setAutoSyncPaused(bool paused);
    void rebuildLocalMonitors();
    void onLocalChange(const QString &pairId);
    void showRateLimitNotice(const SyncPair &pair, const QDateTime &nextAllowed);
    void openSettings();
    void showAbout();
    void quitApplication();
    void showAndRaise();
    void appendSystemStatus(const QString &message);
    void refreshChromePalette();
    void refreshTaskbarIcon();
    void appendLog(const QString &pairId, const QString &message);
    void refreshCurrentRows();

    bool storeCredentials(SyncPair &pair, bool showError);
    void persistPairConfig();

    ConfigStore m_configStore;
    CredentialStore m_credentials;
    AppConfig m_config;
    SyncController *m_controller = nullptr;
    TrayController *m_tray = nullptr;

    QTreeWidget *m_pairsWidget = nullptr;
    QStackedWidget *m_detailStack = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_messageLabel = nullptr;
    QLabel *m_localValue = nullptr;
    QLabel *m_remoteValue = nullptr;
    QLabel *m_accountValue = nullptr;
    QLabel *m_policyValue = nullptr;
    QLabel *m_intervalValue = nullptr;
    QLabel *m_rateLimitValue = nullptr;
    QLabel *m_watchLocalValue = nullptr;
    QLabel *m_syncDeletionValue = nullptr;
    QLabel *m_excludesValue = nullptr;
    QLabel *m_statsLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    QPlainTextEdit *m_activity = nullptr;
    QLabel *m_globalStatus = nullptr;
    QLabel *m_queueStatus = nullptr;

    QAction *m_addAction = nullptr;
    QAction *m_editAction = nullptr;
    QAction *m_removeAction = nullptr;
    QAction *m_resetStateAction = nullptr;
    QAction *m_syncAction = nullptr;
    QAction *m_syncAllAction = nullptr;
    QAction *m_cancelAction = nullptr;
    QAction *m_pauseAction = nullptr;
    QAction *m_settingsAction = nullptr;
    QAction *m_quitAction = nullptr;

    QTimer *m_scheduler = nullptr;
    QFileSystemWatcher *m_themeWatcher = nullptr;
    SyncRateLimiter m_rateLimiter;
    QHash<QString, LocalChangeMonitor *> m_localMonitors;
    QHash<QString, QStringList> m_logs;
    QSet<QString> m_notifyOnSuccess;
    QSet<QString> m_deferredAutoSyncs;
    QSet<QString> m_monitorStartFailures;
    QHash<QString, qint64> m_rateLimitNoticeMs;
    QHash<QString, int> m_autoRetryAttempts;
    bool m_busy = false;
    bool m_autoSyncPaused = false;
    bool m_quitting = false;
    bool m_trayHintShown = false;
};

} // namespace NutSync
