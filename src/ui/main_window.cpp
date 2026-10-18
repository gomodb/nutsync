#include "ui/main_window.h"

#include "core/app_paths.h"
#include "ui/app_icons.h"
#include "core/autostart_manager.h"
#include "core/local_change_monitor.h"
#include "core/logger.h"
#include "core/path_utils.h"
#include "core/retry_policy.h"
#include "core/sync_state_store.h"
#include "ui/pair_dialog.h"
#include "ui/sync_controller.h"
#include "ui/tray_controller.h"

#include <QAction>
#include <QBrush>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFileSystemWatcher>
#include <QFont>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QKeySequence>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QScrollArea>
#include <QSslCertificate>
#include <QScrollBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTime>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace NutSync {

namespace {

QString statusText(const SyncPair &pair, bool current, bool queued)
{
    if (current)
        return QObject::tr("同步中…");
    if (queued)
        return QObject::tr("排队中");
    if (!pair.enabled)
        return QObject::tr("已停用");
    if (pair.lastStatus == QLatin1String("success"))
        return QObject::tr("已同步");
    if (pair.lastStatus == QLatin1String("error"))
        return QObject::tr("有错误");
    if (pair.lastStatus == QLatin1String("cancelled"))
        return QObject::tr("已取消");
    if (pair.lastStatus == QLatin1String("syncing"))
        return QObject::tr("上次未完成");
    return QObject::tr("等待同步");
}

QColor statusColor(const SyncPair &pair, bool current, bool queued)
{
    if (current || queued)
        return QColor(0x1e, 0x88, 0xe5);
    if (!pair.enabled)
        return QColor(0x75, 0x75, 0x75);
    if (pair.lastStatus == QLatin1String("error"))
        return QColor(0xc6, 0x28, 0x28);
    if (pair.lastStatus == QLatin1String("success"))
        return QColor(0x2e, 0x7d, 0x32);
    if (pair.lastStatus == QLatin1String("syncing"))
        return QColor(0xef, 0x6c, 0x00);
    return QColor(0x61, 0x61, 0x61);
}

QString statusIcon(const SyncPair &pair, bool current, bool queued)
{
    if (current)
        return QStringLiteral("emblem-synchronizing");
    if (queued)
        return QStringLiteral("chronometer");
    if (!pair.enabled)
        return QStringLiteral("process-stop");
    if (pair.lastStatus == QLatin1String("error"))
        return QStringLiteral("dialog-error");
    if (pair.lastStatus == QLatin1String("success"))
        return QStringLiteral("emblem-default");
    if (pair.lastStatus == QLatin1String("syncing"))
        return QStringLiteral("dialog-warning");
    return QStringLiteral("folder-sync");
}

QString summaryCounts(const SyncSummary &summary)
{
    QStringList parts;
    if (summary.uploaded > 0)
        parts.append(QObject::tr("上传 %1").arg(summary.uploaded));
    if (summary.downloaded > 0)
        parts.append(QObject::tr("下载 %1").arg(summary.downloaded));
    if (summary.deletedRemote > 0)
        parts.append(QObject::tr("远程删除 %1").arg(summary.deletedRemote));
    if (summary.deletedLocal > 0)
        parts.append(QObject::tr("本地删除 %1").arg(summary.deletedLocal));
    if (summary.directoriesCreated > 0)
        parts.append(QObject::tr("新建目录 %1").arg(summary.directoriesCreated));
    if (summary.conflicts > 0)
        parts.append(QObject::tr("冲突 %1").arg(summary.conflicts));
    if (summary.skipped > 0)
        parts.append(QObject::tr("跳过 %1").arg(summary.skipped));
    if (summary.errors > 0)
        parts.append(QObject::tr("错误 %1").arg(summary.errors));
    return parts.join(QStringLiteral(" · "));
}

QString endpointKey(const QString &value)
{
    QString normalized = value.trimmed();
    while (normalized.endsWith(QLatin1Char('/')))
        normalized.chop(1);
    return normalized;
}

void makeMuted(QLabel *label, bool smaller = false, bool larger = false)
{
    label->setForegroundRole(QPalette::PlaceholderText);
    QFont font = label->font();
    if (smaller)
        font.setPointSizeF(font.pointSizeF() * 0.9);
    if (larger)
        font.setPointSizeF(font.pointSizeF() * 1.1);
    label->setFont(font);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    qRegisterMetaType<SyncSummary>("NutSync::SyncSummary");

    m_controller = new SyncController(AppPaths::stateDatabase(), this);
    connect(m_controller, &SyncController::pairStarted, this, &MainWindow::onPairStarted);
    connect(m_controller, &SyncController::logMessage, this, &MainWindow::onLogMessage);
    connect(m_controller, &SyncController::progress, this, &MainWindow::onProgress);
    connect(m_controller, &SyncController::pairFinished, this, &MainWindow::onPairFinished);
    connect(m_controller, &SyncController::busyChanged, this, &MainWindow::onBusyChanged);
    connect(m_controller, &SyncController::queueChanged, this, &MainWindow::onQueueChanged);

    buildActions();
    buildUi();
    loadConfig();
    QString rateError;
    if (!m_rateLimiter.load(&rateError)) {
        Logger::log(QStringLiteral("rate-limit"), rateError);
        appendSystemStatus(rateError);
    }
    setupTray();
    refreshPairs();
    rebuildLocalMonitors();

    m_themeWatcher = new QFileSystemWatcher(this);
    const QString kdeGlobals =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/kdeglobals");
    if (QFileInfo::exists(kdeGlobals))
        m_themeWatcher->addPath(kdeGlobals);
    connect(m_themeWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        // Editors and KDE may replace the file atomically; re-arm the watch
        // before refreshing the panel/taskbar icon.
        QTimer::singleShot(100, this, [this, path]() {
            if (QFileInfo::exists(path) && !m_themeWatcher->files().contains(path))
                m_themeWatcher->addPath(path);
            refreshChromePalette();
        });
    });
    qApp->setQuitOnLastWindowClosed(!(m_tray && m_tray->isAvailable()));

    m_scheduler = new QTimer(this);
    m_scheduler->setInterval(30000);
    connect(m_scheduler, &QTimer::timeout, this, &MainWindow::scheduleTick);
    m_scheduler->start();

    if (m_config.pairs.isEmpty())
        m_globalStatus->setText(tr("添加一个同步对以开始使用 NutSync"));
    else
        m_globalStatus->setText(tr("就绪，共 %1 个同步对").arg(m_config.pairs.size()));

    // Some Breeze/KDE theme combinations give QToolButton a stale, light
    // palette while the rest of the window is dark. Re-apply the application
    // palette after the first style polish so the toolbar stays legible.
    QTimer::singleShot(0, this, &MainWindow::refreshChromePalette);
    QTimer::singleShot(2000, this, &MainWindow::syncStartupPairs);
}

MainWindow::~MainWindow()
{
    if (m_controller)
        m_controller->cancelCurrent();
}

bool MainWindow::shouldStartHidden() const
{
    return m_config.settings.startMinimized && hasTray() && !m_config.pairs.isEmpty();
}

bool MainWindow::hasTray() const
{
    return m_tray && m_tray->isAvailable();
}

void MainWindow::buildActions()
{
    m_addAction = new QAction(QIcon::fromTheme(QStringLiteral("list-add")), tr("添加同步对"), this);
    m_addAction->setShortcut(QKeySequence::New);
    m_addAction->setToolTip(tr("创建一个本地文件夹与 WebDAV 目录之间的同步对"));

    m_editAction = new QAction(QIcon::fromTheme(QStringLiteral("document-edit")), tr("编辑"), this);
    m_removeAction = new QAction(QIcon::fromTheme(QStringLiteral("edit-delete")), tr("移除"), this);
    m_resetStateAction = new QAction(QIcon::fromTheme(QStringLiteral("edit-clear-history")),
                                     tr("清空同步基线…"), this);

    m_syncAction =
        new QAction(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("立即同步"), this);
    m_syncAction->setShortcut(QKeySequence(Qt::Key_F5));
    m_syncAllAction =
        new QAction(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("全部同步"), this);
    m_syncAllAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F5")));
    m_cancelAction =
        new QAction(QIcon::fromTheme(QStringLiteral("process-stop")), tr("取消同步"), this);
    m_cancelAction->setEnabled(false);

    m_pauseAction = new QAction(tr("暂停自动同步"), this);
    m_pauseAction->setCheckable(true);

    m_settingsAction =
        new QAction(QIcon::fromTheme(QStringLiteral("configure")), tr("首选项"), this);
    m_quitAction =
        new QAction(QIcon::fromTheme(QStringLiteral("application-exit")), tr("退出"), this);
    m_quitAction->setShortcut(QKeySequence::Quit);

    connect(m_addAction, &QAction::triggered, this, &MainWindow::addPair);
    connect(m_editAction, &QAction::triggered, this, &MainWindow::editPair);
    connect(m_removeAction, &QAction::triggered, this, &MainWindow::removePair);
    connect(m_resetStateAction, &QAction::triggered, this, &MainWindow::resetPairState);
    connect(m_syncAction, &QAction::triggered, this, &MainWindow::requestSyncSelected);
    connect(m_syncAllAction, &QAction::triggered, this, &MainWindow::requestSyncAll);
    connect(m_cancelAction, &QAction::triggered, this, &MainWindow::cancelSync);
    connect(m_pauseAction, &QAction::toggled, this, &MainWindow::setAutoSyncPaused);
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::openSettings);
    connect(m_quitAction, &QAction::triggered, this, &MainWindow::quitApplication);
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("NutSync"));
    setWindowIcon(AppIcons::colored());
    resize(1080, 720);
    setMinimumSize(820, 540);

    auto *toolbar = addToolBar(tr("主工具栏"));
    toolbar->setObjectName(QStringLiteral("MainToolBar"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->addAction(m_addAction);
    toolbar->addAction(m_editAction);
    toolbar->addAction(m_removeAction);
    toolbar->addSeparator();
    toolbar->addAction(m_syncAction);
    toolbar->addAction(m_syncAllAction);
    toolbar->addAction(m_cancelAction);
    toolbar->addSeparator();
    toolbar->addAction(m_settingsAction);

    auto *fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    fileMenu->addAction(m_addAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);

    auto *syncMenu = menuBar()->addMenu(tr("同步(&S)"));
    syncMenu->addAction(m_syncAction);
    syncMenu->addAction(m_syncAllAction);
    syncMenu->addAction(m_cancelAction);
    syncMenu->addSeparator();
    syncMenu->addAction(m_pauseAction);

    auto *editMenu = menuBar()->addMenu(tr("编辑(&E)"));
    editMenu->addAction(m_editAction);
    editMenu->addAction(m_removeAction);
    editMenu->addAction(m_resetStateAction);
    editMenu->addSeparator();
    editMenu->addAction(m_settingsAction);

    auto *helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    auto *aboutAction =
        helpMenu->addAction(QIcon::fromTheme(QStringLiteral("help-about")), tr("关于 NutSync"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    auto *leftPanel = new QWidget(splitter);
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(12, 12, 6, 12);
    leftLayout->setSpacing(8);

    auto *leftHeader = new QLabel(tr("同步对"), leftPanel);
    QFont leftFont = leftHeader->font();
    leftFont.setBold(true);
    leftFont.setPointSize(leftFont.pointSize() + 1);
    leftHeader->setFont(leftFont);
    leftLayout->addWidget(leftHeader);

    m_pairsWidget = new QTreeWidget(leftPanel);
    m_pairsWidget->setColumnCount(3);
    m_pairsWidget->setHeaderLabels({tr("名称"), tr("状态"), tr("上次同步")});
    m_pairsWidget->setRootIsDecorated(false);
    m_pairsWidget->setAlternatingRowColors(true);
    m_pairsWidget->setUniformRowHeights(true);
    m_pairsWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pairsWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pairsWidget->setIconSize(QSize(22, 22));
    m_pairsWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_pairsWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_pairsWidget->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_pairsWidget->setContextMenuPolicy(Qt::ActionsContextMenu);
    m_pairsWidget->addAction(m_syncAction);
    m_pairsWidget->addAction(m_editAction);
    m_pairsWidget->addAction(m_removeAction);
    m_pairsWidget->addAction(m_resetStateAction);
    leftLayout->addWidget(m_pairsWidget, 1);

    auto *hint =
        new QLabel(tr("提示：双击同步对可编辑。关闭窗口后 NutSync 会继续在托盘运行。"), leftPanel);
    hint->setWordWrap(true);
    makeMuted(hint, true);
    leftLayout->addWidget(hint);

    auto *rightPanel = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(18, 16, 18, 16);
    rightLayout->setSpacing(10);

    m_detailStack = new QStackedWidget(rightPanel);

    m_emptyLabel = new QLabel(tr("选择左侧的同步对，或点击工具栏中的“添加同步对”开始。\n\n"
                                 "NutSync 使用三路状态记录进行双向同步：只有一侧变化时会自动传播，"
                                 "两侧都变化时按所选策略处理，默认保留冲突副本。"),
                              m_detailStack);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    makeMuted(m_emptyLabel, false, true);
    m_emptyLabel->setContentsMargins(40, 40, 40, 40);

    auto *details = new QWidget(m_detailStack);
    auto *detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    detailsLayout->setSpacing(10);

    auto *titleRow = new QHBoxLayout();
    m_titleLabel = new QLabel(details);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 5);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_stateLabel = new QLabel(details);
    m_stateLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QFont stateFont = m_stateLabel->font();
    stateFont.setBold(true);
    m_stateLabel->setFont(stateFont);
    titleRow->addWidget(m_titleLabel, 1);
    titleRow->addWidget(m_stateLabel);
    detailsLayout->addLayout(titleRow);

    m_messageLabel = new QLabel(details);
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    makeMuted(m_messageLabel);
    detailsLayout->addWidget(m_messageLabel);

    auto *line = new QFrame(details);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    detailsLayout->addWidget(line);

    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(1, 1);

    auto addField = [&](int row, const QString &label, QLabel **value) {
        auto *labelWidget = new QLabel(label, details);
        makeMuted(labelWidget);
        labelWidget->setAlignment(Qt::AlignRight | Qt::AlignTop);
        *value = new QLabel(details);
        (*value)->setWordWrap(true);
        (*value)->setTextInteractionFlags(Qt::TextSelectableByMouse);
        grid->addWidget(labelWidget, row, 0);
        grid->addWidget(*value, row, 1);
    };
    addField(0, tr("本地文件夹"), &m_localValue);
    addField(1, tr("WebDAV 目录"), &m_remoteValue);
    addField(2, tr("账户"), &m_accountValue);
    addField(3, tr("冲突处理"), &m_policyValue);
    addField(4, tr("定时同步"), &m_intervalValue);
    addField(5, tr("请求频率"), &m_rateLimitValue);
    addField(6, tr("本地监视"), &m_watchLocalValue);
    addField(7, tr("同步删除"), &m_syncDeletionValue);
    addField(8, tr("排除规则"), &m_excludesValue);
    detailsLayout->addLayout(grid);

    m_progress = new QProgressBar(details);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    m_progress->setVisible(false);
    detailsLayout->addWidget(m_progress);

    m_statsLabel = new QLabel(details);
    makeMuted(m_statsLabel);
    detailsLayout->addWidget(m_statsLabel);

    auto *detailButtons = new QHBoxLayout();
    auto *syncButton =
        new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("立即同步"), details);
    syncButton->setObjectName(QStringLiteral("detailSyncButton"));
    auto *editButton =
        new QPushButton(QIcon::fromTheme(QStringLiteral("document-edit")), tr("编辑"), details);
    editButton->setObjectName(QStringLiteral("detailEditButton"));
    detailButtons->addWidget(syncButton);
    detailButtons->addWidget(editButton);
    detailButtons->addStretch(1);
    detailsLayout->addLayout(detailButtons);

    auto *activityLabel = new QLabel(tr("活动"), details);
    QFont activityFont = activityLabel->font();
    activityFont.setBold(true);
    activityLabel->setFont(activityFont);
    detailsLayout->addWidget(activityLabel);

    m_activity = new QPlainTextEdit(details);
    m_activity->setReadOnly(true);
    m_activity->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_activity->setMaximumBlockCount(2000);
    m_activity->setPlaceholderText(tr("此同步对的同步日志会显示在这里。"));
    QFont logFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (logFont.pointSizeF() > 0)
        logFont.setPointSizeF(logFont.pointSizeF() * 0.9);
    m_activity->setFont(logFont);
    detailsLayout->addWidget(m_activity, 1);

    m_detailStack->addWidget(m_emptyLabel);
    m_detailStack->addWidget(details);
    rightLayout->addWidget(m_detailStack, 1);

    splitter->addWidget(leftPanel);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({360, 720});
    setCentralWidget(splitter);

    m_globalStatus = new QLabel(this);
    m_queueStatus = new QLabel(this);
    statusBar()->addWidget(m_globalStatus, 1);
    statusBar()->addPermanentWidget(m_queueStatus);

    connect(m_pairsWidget, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *, QTreeWidgetItem *) {
                updateDetails();
                updateActions();
            });
    connect(m_pairsWidget, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *, int) {
        editPair();
    });
    connect(syncButton, &QPushButton::clicked, this, &MainWindow::requestSyncSelected);
    connect(editButton, &QPushButton::clicked, this, &MainWindow::editPair);

    updateActions();
}

void MainWindow::loadConfig()
{
    QString error;
    const bool loaded = m_configStore.load(&m_config, &error);
    if (!loaded) {
        const QString broken = m_configStore.filePath() + QStringLiteral(".broken-")
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        QFile::rename(m_configStore.filePath(), broken);
        QMessageBox::critical(this, tr("配置文件读取失败"),
                              tr("%1\n\n损坏的配置已备份为：\n%2\n\nNutSync 将以空配置启动。")
                                  .arg(error, PathUtils::nativePath(broken)));
    }
    m_pauseAction->setChecked(m_autoSyncPaused);
    if (loaded) {
        // Persist migrations such as removing credentials embedded in WebDAV URLs.
        saveConfig(false);
    }
}

void MainWindow::setupTray()
{
    m_tray = new TrayController(m_config.settings.notifications, this);
    connect(m_tray, &TrayController::showRequested, this, &MainWindow::showAndRaise);
    connect(m_tray, &TrayController::syncAllRequested, this, &MainWindow::requestSyncAll);
    connect(m_tray, &TrayController::quitRequested, this, &MainWindow::quitApplication);
    connect(m_tray, &TrayController::autoSyncToggled, this, &MainWindow::setAutoSyncPaused);
    m_tray->setAutoSyncPaused(m_autoSyncPaused);
    updateTrayIdleStatus();
}

void MainWindow::saveConfig(bool showError)
{
    QString error;
    if (!m_configStore.save(m_config, &error)) {
        appendSystemStatus(tr("保存配置失败：%1").arg(error));
        if (showError) {
            QMessageBox::warning(this, tr("无法保存配置"), error);
        }
        Logger::log(QStringLiteral("config"), error);
    }
}

void MainWindow::persistPairConfig()
{
    saveConfig(true);
}

void MainWindow::refreshPairs(const QString &selectId)
{
    QString wanted = selectId;
    if (wanted.isEmpty())
        wanted = selectedPairId();

    const QSignalBlocker blocker(m_pairsWidget);
    m_pairsWidget->clear();
    QTreeWidgetItem *toSelect = nullptr;
    for (const SyncPair &pair : std::as_const(m_config.pairs)) {
        auto *item = new QTreeWidgetItem(m_pairsWidget);
        item->setData(0, Qt::UserRole, pair.id);
        item->setText(0, pair.name);
        item->setIcon(0, QIcon::fromTheme(QStringLiteral("folder-sync")));
        item->setText(1, statusText(pair, false, false));
        item->setText(2,
                      pair.lastSync.isValid()
                          ? pair.lastSync.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                          : QStringLiteral("—"));
        item->setTextAlignment(1, Qt::AlignCenter);
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        if (pair.id == wanted)
            toSelect = item;
        updatePairRow(pair);
    }

    if (!toSelect && m_pairsWidget->topLevelItemCount() > 0)
        toSelect = m_pairsWidget->topLevelItem(0);
    m_pairsWidget->setCurrentItem(toSelect);
    if (toSelect)
        m_pairsWidget->scrollToItem(toSelect);

    updateDetails();
    updateActions();
}

void MainWindow::updatePairRow(const SyncPair &pair)
{
    if (!m_pairsWidget)
        return;
    QTreeWidgetItem *item = nullptr;
    for (int index = 0; index < m_pairsWidget->topLevelItemCount(); ++index) {
        QTreeWidgetItem *candidate = m_pairsWidget->topLevelItem(index);
        if (candidate->data(0, Qt::UserRole).toString() == pair.id) {
            item = candidate;
            break;
        }
    }
    if (!item)
        return;

    const bool current = m_controller && m_controller->currentPairId() == pair.id;
    const bool queued = m_controller && !current && m_controller->contains(pair.id);
    item->setText(0, pair.name);
    item->setIcon(0,
                  QIcon::fromTheme(statusIcon(pair, current, queued),
                                   QIcon(QStringLiteral(":/resources/icons/nutsync.svg"))));
    item->setText(1, statusText(pair, current, queued));
    item->setForeground(1, statusColor(pair, current, queued));
    item->setText(2,
                  pair.lastSync.isValid()
                      ? pair.lastSync.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                      : QStringLiteral("—"));
    const QString tooltip = pair.lastError.isEmpty() ? pair.lastMessage : pair.lastError;
    item->setToolTip(0, tooltip);
    item->setToolTip(1, tooltip);
}

void MainWindow::updateDetails()
{
    if (!m_detailStack)
        return;
    const SyncPair *pair = findPair(selectedPairId());
    if (!pair) {
        m_detailStack->setCurrentWidget(m_emptyLabel);
        m_titleLabel->clear();
        m_activity->clear();
        return;
    }

    m_detailStack->setCurrentIndex(1);
    m_titleLabel->setText(pair->name);
    const bool current = m_controller && m_controller->currentPairId() == pair->id;
    const bool queued = m_controller && !current && m_controller->contains(pair->id);
    m_stateLabel->setText(statusText(*pair, current, queued));
    const QColor color = statusColor(*pair, current, queued);
    m_stateLabel->setStyleSheet(QStringLiteral("color: %1;").arg(color.name()));

    if (!pair->lastError.isEmpty())
        m_messageLabel->setText(tr("最近错误：%1").arg(pair->lastError));
    else if (!pair->lastMessage.isEmpty())
        m_messageLabel->setText(pair->lastMessage);
    else
        m_messageLabel->setText(tr("尚未进行过同步。"));

    m_localValue->setText(PathUtils::nativePath(pair->localPath));
    m_localValue->setToolTip(pair->localPath);
    const QUrl remote(pair->remoteUrl);
    m_remoteValue->setText(remote.isValid() ? remote.toDisplayString(QUrl::RemovePassword)
                                            : pair->remoteUrl);
    m_remoteValue->setToolTip(m_remoteValue->text());
    m_accountValue->setText(pair->username.isEmpty() ? tr("匿名访问") : pair->username);
    m_policyValue->setText(conflictPolicyDisplayName(pair->conflictPolicy));
    m_intervalValue->setText(pair->intervalMinutes == 0
                                 ? tr("不启用定时同步")
                                 : tr("每 %1 分钟").arg(pair->intervalMinutes));
    m_rateLimitValue->setText(rateLimitDescription(
        pair->rateLimitEnabled, pair->rateLimitWindowMinutes, pair->rateLimitMaxRequests));
    m_watchLocalValue->setText(pair->watchLocalChanges ? tr("inotify 自动触发") : tr("已关闭"));
    m_syncDeletionValue->setText(pair->syncDeletions ? tr("本地删除传播到远程")
                                                     : tr("关闭（下次同步恢复）"));
    m_excludesValue->setText(pair->excludePatterns.isEmpty()
                                 ? tr("无")
                                 : pair->excludePatterns.join(QStringLiteral("，")));

    m_statsLabel->setText(pair->lastSync.isValid() ? tr("上次同步：%1")
                                                         .arg(pair->lastSync.toLocalTime().toString(
                                                             QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                                                   : tr("还没有同步记录"));

    m_progress->setVisible(current);
    if (current) {
        if (m_progress->maximum() == 0)
            m_progress->setRange(0, 0);
    }

    const QStringList logs = m_logs.value(pair->id);
    m_activity->setPlainText(logs.join(QLatin1Char('\n')));
    m_activity->verticalScrollBar()->setValue(m_activity->verticalScrollBar()->maximum());
}

void MainWindow::updateActions()
{
    const QString id = selectedPairId();
    const SyncPair *pair = findPair(id);
    const bool busy = m_controller && m_controller->isBusy();
    const bool inQueue = m_controller && !id.isEmpty() && m_controller->contains(id);

    m_editAction->setEnabled(pair && !inQueue);
    m_removeAction->setEnabled(pair && !inQueue);
    m_resetStateAction->setEnabled(pair && !inQueue);
    m_syncAction->setEnabled(pair && !inQueue);
    m_syncAllAction->setEnabled(!m_config.pairs.isEmpty());
    m_cancelAction->setEnabled(busy);
    m_pauseAction->setChecked(m_autoSyncPaused);

    if (auto *button = findChild<QPushButton *>(QStringLiteral("detailSyncButton")))
        button->setEnabled(pair && !inQueue);
    if (auto *button = findChild<QPushButton *>(QStringLiteral("detailEditButton")))
        button->setEnabled(pair && !inQueue);

    m_queueStatus->setText(m_controller && m_controller->pendingCount() > 0
                               ? tr("队列：%1").arg(m_controller->pendingCount())
                               : QString());
}

SyncPair *MainWindow::findPair(const QString &id)
{
    for (SyncPair &pair : m_config.pairs) {
        if (pair.id == id)
            return &pair;
    }
    return nullptr;
}

const SyncPair *MainWindow::findPair(const QString &id) const
{
    for (const SyncPair &pair : m_config.pairs) {
        if (pair.id == id)
            return &pair;
    }
    return nullptr;
}

QString MainWindow::selectedPairId() const
{
    if (!m_pairsWidget || !m_pairsWidget->currentItem())
        return {};
    return m_pairsWidget->currentItem()->data(0, Qt::UserRole).toString();
}

void MainWindow::selectPair(const QString &id)
{
    for (int index = 0; index < m_pairsWidget->topLevelItemCount(); ++index) {
        QTreeWidgetItem *item = m_pairsWidget->topLevelItem(index);
        if (item->data(0, Qt::UserRole).toString() == id) {
            m_pairsWidget->setCurrentItem(item);
            return;
        }
    }
}

bool MainWindow::storeCredentials(SyncPair &pair, bool showError)
{
    QString error;
    if (!pair.rememberPassword) {
        m_credentials.deletePassword(pair.id, &error);
        if (!error.isEmpty() && showError)
            QMessageBox::warning(this, tr("钥匙串"), error);
        return true;
    }
    if (!m_credentials.isAvailable()) {
        pair.rememberPassword = false;
        if (showError) {
            QMessageBox::warning(
                this, tr("无法保存密码"),
                tr("当前构建未启用系统钥匙串。密码不会被保存，自动同步可能无法通过认证。"));
        }
        return false;
    }
    if (pair.password.isEmpty()) {
        m_credentials.deletePassword(pair.id, &error);
        return true;
    }
    if (!m_credentials.writePassword(pair.id, pair.password, &error)) {
        pair.rememberPassword = false;
        if (showError)
            QMessageBox::warning(this, tr("无法保存密码"), error);
        return false;
    }
    return true;
}

void MainWindow::addPair()
{
    SyncPair pair;
    pair.intervalMinutes = 15;
    pair.enabled = true;
    pair.syncOnStartup = true;

    PairDialog dialog(pair, m_credentials.isAvailable(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    SyncPair created = dialog.syncPair();
    if (created.rememberPassword && !m_credentials.isAvailable()) {
        created.rememberPassword = false;
        QMessageBox::information(
            this, tr("密码未保存"),
            tr("当前构建未启用系统钥匙串。同步对已添加，但密码只会在本次运行中保留。"));
    }
    storeCredentials(created, true);
    m_config.pairs.append(created);
    persistPairConfig();
    refreshPairs(created.id);
    rebuildLocalMonitors();
    appendSystemStatus(tr("已添加同步对“%1”").arg(created.name));
}

void MainWindow::editPair()
{
    const QString id = selectedPairId();
    if (id.isEmpty())
        return;
    editCurrentPair(id);
}

void MainWindow::editCurrentPair(const QString &id)
{
    SyncPair *pair = findPair(id);
    if (!pair)
        return;
    if (m_controller->contains(id)) {
        QMessageBox::information(this, tr("正在同步"),
                                 tr("此同步对正在同步或排队，请完成后再编辑。"));
        return;
    }

    SyncPair editing = *pair;
    if (editing.rememberPassword && editing.password.isEmpty()) {
        QString error;
        editing.password = m_credentials.readPassword(editing.id, &error);
        if (!error.isEmpty())
            appendSystemStatus(tr("读取密码失败：%1").arg(error));
    }

    PairDialog dialog(editing, m_credentials.isAvailable(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    SyncPair updated = dialog.syncPair();
    const bool endpointChanged =
        QDir::cleanPath(updated.localPath) != QDir::cleanPath(editing.localPath)
        || endpointKey(updated.remoteUrl) != endpointKey(editing.remoteUrl);
    if (endpointChanged) {
        const auto answer = QMessageBox::question(
            this, tr("同步位置已更改"),
            tr("本地文件夹或 WebDAV 地址发生了变化。\n\n"
               "继续将清空此同步对的基线状态，不会删除任何文件。"
               "下一次同步会把两侧内容视为首次同步，并保留冲突副本。\n\n是否继续？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
        if (!clearPairState(id))
            return;
        updated.lastSync = {};
        updated.lastStatus.clear();
        updated.lastMessage.clear();
        updated.lastError.clear();
        QString rateError;
        if (!m_rateLimiter.clear(id, &rateError))
            appendLog(id, tr("清空请求频率记录失败：%1").arg(rateError));
        m_deferredAutoSyncs.remove(id);
    }

    if (updated.rememberPassword && !m_credentials.isAvailable())
        updated.rememberPassword = false;
    storeCredentials(updated, true);
    *pair = updated;
    persistPairConfig();
    refreshPairs(id);
    rebuildLocalMonitors();
    appendSystemStatus(tr("已更新同步对“%1”").arg(updated.name));
}

void MainWindow::resetPairState()
{
    const QString id = selectedPairId();
    SyncPair *pair = findPair(id);
    if (!pair)
        return;
    if (m_controller->contains(id)) {
        QMessageBox::information(this, tr("正在同步"),
                                 tr("此同步对正在同步或排队，请完成后再清空基线。"));
        return;
    }
    const auto answer =
        QMessageBox::question(this, tr("清空同步基线"),
                              tr("清空“%1”的同步基线后，下次同步会被视为首次同步。\n\n"
                                 "不会删除任何本地或远程文件；两侧都存在的不同文件会生成冲突副本。"
                                 "\n\n是否继续？")
                                  .arg(pair->name),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    if (!clearPairState(id))
        return;
    pair->lastSync = {};
    pair->lastStatus.clear();
    pair->lastMessage = tr("同步基线已清空，等待首次同步");
    pair->lastError.clear();
    saveConfig(true);
    refreshPairs(id);
    appendSystemStatus(tr("已清空同步基线：%1").arg(pair->name));
}

void MainWindow::cancelRetryTimer(const QString &id)
{
    if (QTimer *timer = m_retryTimers.take(id)) {
        timer->stop();
        timer->deleteLater();
    }
}

void MainWindow::cancelRetry(const QString &id)
{
    cancelRetryTimer(id);
    m_autoRetryAttempts.remove(id);
}

void MainWindow::removePair()
{
    const QString id = selectedPairId();
    if (!id.isEmpty())
        removePairById(id);
}

void MainWindow::removePairById(const QString &id)
{
    const SyncPair *pair = findPair(id);
    if (!pair)
        return;
    if (m_controller->contains(id)) {
        QMessageBox::information(this, tr("正在同步"),
                                 tr("此同步对正在同步或排队，请完成后再移除。"));
        return;
    }

    const auto answer =
        QMessageBox::question(this, tr("移除同步对"),
                              tr("确定移除同步对“%1”吗？\n\n"
                                 "只会删除 NutSync 的配置和同步基线，不会删除本地或远程文件。")
                                  .arg(pair->name),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    cancelRetry(id);
    clearPairState(id);
    QString error;
    m_credentials.deletePassword(id, &error);
    if (!m_rateLimiter.clear(id, &error))
        Logger::log(QStringLiteral("rate-limit"), error);
    m_deferredAutoSyncs.remove(id);
    m_rateLimitNoticeMs.remove(id);
    m_config.pairs.erase(std::remove_if(m_config.pairs.begin(), m_config.pairs.end(),
                                        [&id](const SyncPair &candidate) {
                                            return candidate.id == id;
                                        }),
                         m_config.pairs.end());
    persistPairConfig();
    refreshPairs();
    rebuildLocalMonitors();
    appendSystemStatus(tr("已移除同步对"));
}

bool MainWindow::clearPairState(const QString &id, bool showError)
{
    SyncStateStore store(AppPaths::stateDatabase());
    QString error;
    if (!store.open(&error) || !store.clearPair(id, &error)) {
        if (showError)
            QMessageBox::warning(this, tr("清空同步基线失败"), error);
        return false;
    }
    return true;
}

void MainWindow::rebuildLocalMonitors()
{
    QSet<QString> validIds;
    for (const SyncPair &pair : std::as_const(m_config.pairs)) {
        if (!pair.enabled || !pair.watchLocalChanges || pair.localPath.isEmpty())
            continue;
        validIds.insert(pair.id);

        QString expectedRoot = QFileInfo(pair.localPath).canonicalFilePath();
        if (expectedRoot.isEmpty())
            expectedRoot = QDir::cleanPath(pair.localPath);
        LocalChangeMonitor *monitor = m_localMonitors.value(pair.id);
        if (monitor
            && (monitor->rootPath() != expectedRoot
                || monitor->excludePatterns() != pair.excludePatterns)) {
            m_localMonitors.remove(pair.id);
            monitor->stop();
            monitor->deleteLater();
            monitor = nullptr;
        }
        if (monitor) {
            if (monitor->isActive() && monitor->watchCount() == 0)
                monitor->refreshWatches();
            continue;
        }

        monitor = new LocalChangeMonitor(pair.localPath, pair.excludePatterns, this);
        const QString pairId = pair.id;
        connect(monitor, &LocalChangeMonitor::localChanged, this, [this, pairId]() {
            onLocalChange(pairId);
        });
        connect(monitor, &LocalChangeMonitor::monitorError, this,
                [this, pairId](const QString &message) {
                    appendLog(pairId, tr("文件监视：%1").arg(message));
                });
        QString error;
        if (!monitor->start(&error)) {
            if (!m_monitorStartFailures.contains(pair.id)) {
                m_monitorStartFailures.insert(pair.id);
                appendLog(pair.id, tr("无法启用 inotify 文件监视：%1").arg(error));
            }
            delete monitor;
            continue;
        }
        if (m_monitorStartFailures.remove(pair.id) > 0)
            appendLog(pair.id, tr("inotify 文件监视已恢复"));
        m_localMonitors.insert(pair.id, monitor);
    }

    const QStringList monitoredIds = m_localMonitors.keys();
    for (const QString &id : monitoredIds) {
        if (validIds.contains(id))
            continue;
        LocalChangeMonitor *monitor = m_localMonitors.take(id);
        monitor->stop();
        monitor->deleteLater();
        m_deferredAutoSyncs.remove(id);
        m_rateLimitNoticeMs.remove(id);
        m_monitorStartFailures.remove(id);
    }
    for (const QString &id : std::as_const(m_deferredAutoSyncs)) {
        if (!validIds.contains(id))
            m_deferredAutoSyncs.remove(id);
    }
    const QStringList failedIds = m_monitorStartFailures.values();
    for (const QString &id : failedIds) {
        if (!validIds.contains(id))
            m_monitorStartFailures.remove(id);
    }
}

void MainWindow::onLocalChange(const QString &pairId)
{
    const SyncPair *pair = findPair(pairId);
    if (!pair || !pair->enabled)
        return;
    m_deferredAutoSyncs.insert(pairId);
    if (m_autoSyncPaused)
        return;
    appendLog(pairId, tr("检测到本地文件变化"));
    requestSync(pairId, false);
}

void MainWindow::requestSync(const QString &id, bool manual)
{
    const SyncPair *pair = findPair(id);
    if (!pair)
        return;
    if (!pair->enabled) {
        if (manual)
            appendSystemStatus(tr("同步对“%1”已停用").arg(pair->name));
        return;
    }
    if (m_controller->contains(id)) {
        if (manual)
            appendSystemStatus(tr("同步对“%1”已在队列中").arg(pair->name));
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    if (pair->rateLimitEnabled && !manual) {
        QDateTime nextAllowed;
        if (!m_rateLimiter.isAllowed(*pair, now, &nextAllowed)) {
            m_deferredAutoSyncs.insert(id);
            showRateLimitNotice(*pair, nextAllowed);
            return;
        }
    }

    if (manual) {
        cancelRetry(id);
        m_notifyOnSuccess.insert(id);
    }
    m_controller->enqueue(*pair);
    if (pair->rateLimitEnabled) {
        QString error;
        if (!m_rateLimiter.recordStart(*pair, now, &error)) {
            Logger::log(QStringLiteral("rate-limit"), error);
            appendLog(id, tr("记录请求频率失败：%1").arg(error));
        }
    }
    m_deferredAutoSyncs.remove(id);
    updatePairRow(*pair);
    updateActions();
    appendSystemStatus(tr("已加入同步队列：%1").arg(pair->name));
}

void MainWindow::showRateLimitNotice(const SyncPair &pair, const QDateTime &nextAllowed)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 lastNotice = m_rateLimitNoticeMs.value(pair.id);
    if (lastNotice > 0 && nowMs - lastNotice < 5 * 60 * 1000)
        return;
    m_rateLimitNoticeMs.insert(pair.id, nowMs);

    QString retry = tr("稍后");
    if (nextAllowed.isValid()) {
        const QDateTime local = nextAllowed.toLocalTime();
        retry = local.date() == QDate::currentDate()
            ? local.toString(QStringLiteral("HH:mm:ss"))
            : local.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    }
    const QString message =
        tr("“%1”已达到请求频率上限（%2），自动同步推迟到 %3 之后")
            .arg(pair.name,
                 rateLimitDescription(pair.rateLimitEnabled, pair.rateLimitWindowMinutes,
                                      pair.rateLimitMaxRequests),
                 retry);
    appendLog(pair.id, message);
    appendSystemStatus(message);
}

void MainWindow::requestSyncSelected()
{
    const QString id = selectedPairId();
    if (!id.isEmpty())
        requestSync(id, true);
}

void MainWindow::requestSyncAll()
{
    bool queuedAny = false;
    for (const SyncPair &pair : std::as_const(m_config.pairs)) {
        if (!pair.enabled || m_controller->contains(pair.id))
            continue;
        requestSync(pair.id, true);
        queuedAny = true;
    }
    if (!queuedAny)
        appendSystemStatus(tr("没有可同步的条目"));
}

void MainWindow::cancelSync()
{
    if (!m_controller->isBusy())
        return;
    m_controller->cancelCurrent();
    m_globalStatus->setText(tr("正在取消同步…"));
    refreshPairs(selectedPairId());
}

void MainWindow::onPairStarted(const QString &pairId)
{
    const SyncPair *pair = findPair(pairId);
    if (!pair)
        return;
    if (LocalChangeMonitor *monitor = m_localMonitors.value(pairId))
        monitor->setSuppressed(true);
    m_deferredAutoSyncs.remove(pairId);
    updatePairRow(*pair);
    if (selectedPairId() == pairId) {
        m_progress->setRange(0, 0);
        m_progress->setVisible(true);
        m_statsLabel->setText(tr("正在扫描本地与远程内容…"));
        updateDetails();
    }
    m_globalStatus->setText(tr("正在同步“%1”…").arg(pair->name));
    if (m_tray)
        m_tray->setStatus(QStringLiteral("NutSync"), tr("正在同步：%1").arg(pair->name),
                          QStringLiteral("emblem-synchronizing"));
}

void MainWindow::onLogMessage(const QString &pairId, const QString &message)
{
    appendLog(pairId, message);
}

void MainWindow::appendLog(const QString &pairId, const QString &message)
{
    const QString line = QStringLiteral("[%1] %2").arg(
        QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), message);
    QStringList &logs = m_logs[pairId];
    logs.append(line);
    if (logs.size() > 800)
        logs = logs.mid(logs.size() - 800);
    if (selectedPairId() == pairId)
        m_activity->appendPlainText(line);
    Logger::log(QStringLiteral("activity"), QStringLiteral("%1: %2").arg(pairId, message));
}

void MainWindow::onProgress(const QString &pairId, int completed, int total, const QString &path)
{
    if (selectedPairId() != pairId)
        return;
    if (total <= 0) {
        m_progress->setRange(0, 0);
        m_progress->setVisible(true);
        return;
    }
    m_progress->setRange(0, total);
    m_progress->setValue(completed);
    m_progress->setVisible(true);
    if (!path.isEmpty())
        m_statsLabel->setText(tr("正在处理 %1/%2：%3").arg(completed).arg(total).arg(path));
    else
        m_statsLabel->setText(tr("正在处理 %1/%2").arg(completed).arg(total));
}

void MainWindow::onPairFinished(const SyncSummary &summary)
{
    SyncPair *pair = findPair(summary.pairId);
    if (!pair)
        return;

    pair->lastSync = QDateTime::currentDateTime();
    pair->lastStatus = summary.cancelled
        ? QStringLiteral("cancelled")
        : (summary.success ? QStringLiteral("success") : QStringLiteral("error"));
    QString message = summary.message;
    const QString counts = summaryCounts(summary);
    if (!counts.isEmpty())
        message += QStringLiteral(" · ") + counts;
    pair->lastMessage = message;
    pair->lastError = summary.errorDetails.value(0);
    saveConfig(false);

    updatePairRow(*pair);
    if (selectedPairId() == pair->id) {
        m_progress->setVisible(false);
        m_progress->setRange(0, 100);
        m_progress->setValue(summary.success ? 100 : 0);
        updateDetails();
    }
    m_globalStatus->setText(tr("%1：%2").arg(pair->name, summary.message));

    if (m_tray) {
        if (!summary.success && !summary.cancelled) {
            m_tray->showNotification(tr("NutSync 同步失败"),
                                     summary.errorDetails.value(0, summary.message), true);
        } else if (summary.conflicts > 0) {
            m_tray->showNotification(tr("NutSync 同步完成"),
                                     tr("“%1”处理了 %2 个冲突。%3")
                                         .arg(pair->name)
                                         .arg(summary.conflicts)
                                         .arg(summary.message),
                                     false);
        } else if (m_notifyOnSuccess.contains(pair->id)) {
            const QString detail = counts.isEmpty()
                ? summary.message
                : summary.message + QStringLiteral(" · ") + counts;
            m_tray->showNotification(tr("NutSync 同步完成"), tr("“%1”：%2").arg(pair->name, detail),
                                     false);
        }
    }
    const bool manualSync = m_notifyOnSuccess.contains(summary.pairId);
    if (summary.success || summary.cancelled || manualSync
        || RetryPolicy::hasPermanentHttpError(summary)) {
        cancelRetry(summary.pairId);
    } else if (pair->enabled && m_config.settings.retryCount > 0) {
        const int attempt = m_autoRetryAttempts.value(summary.pairId) + 1;
        if (attempt <= m_config.settings.retryCount) {
            m_autoRetryAttempts.insert(summary.pairId, attempt);
            const int delay =
                RetryPolicy::backoffSeconds(m_config.settings.retryDelaySeconds, attempt);
            appendLog(summary.pairId,
                      tr("同步失败，%1 秒后重试（%2/%3）")
                          .arg(delay)
                          .arg(attempt)
                          .arg(m_config.settings.retryCount));
            cancelRetryTimer(summary.pairId);
            auto *timer = new QTimer(this);
            timer->setSingleShot(true);
            connect(timer, &QTimer::timeout, this, [this, pairId = summary.pairId]() {
                m_retryTimers.remove(pairId);
                const SyncPair *current = findPair(pairId);
                if (current && current->enabled && m_config.settings.retryCount > 0
                    && m_autoRetryAttempts.value(pairId) <= m_config.settings.retryCount
                    && !m_controller->contains(pairId)) {
                    requestSync(pairId, false);
                }
            });
            m_retryTimers.insert(summary.pairId, timer);
            timer->start(delay * 1000);
        } else {
            cancelRetry(summary.pairId);
        }
    } else {
        cancelRetry(summary.pairId);
    }
    m_notifyOnSuccess.remove(summary.pairId);
    updateActions();

    const QString pairId = pair->id;
    QPointer<LocalChangeMonitor> monitor = m_localMonitors.value(pairId);
    if (monitor) {
        QTimer::singleShot(2000, this, [this, pairId, monitor]() {
            if (!monitor || m_localMonitors.value(pairId) != monitor.data())
                return;
            monitor->refreshWatches();
            if (!m_controller->contains(pairId))
                monitor->setSuppressed(false);
        });
    }
    QTimer::singleShot(800, this, &MainWindow::updateTrayIdleStatus);
}

void MainWindow::onBusyChanged(bool busy)
{
    m_busy = busy;
    updateActions();
    if (m_tray)
        m_tray->setBusy(busy);
    if (!busy) {
        updateTrayIdleStatus();
        if (selectedPairId().isEmpty())
            m_progress->setVisible(false);
        else
            updateDetails();
    }
    updateActions();
}

void MainWindow::onQueueChanged(int pending)
{
    Q_UNUSED(pending)
    updateActions();
    refreshCurrentRows();
}

void MainWindow::refreshCurrentRows()
{
    for (const SyncPair &pair : std::as_const(m_config.pairs))
        updatePairRow(pair);
}

void MainWindow::updateTrayIdleStatus()
{
    if (!m_tray || m_busy)
        return;
    const int enabled =
        std::count_if(m_config.pairs.cbegin(), m_config.pairs.cend(), [](const SyncPair &pair) {
            return pair.enabled;
        });
    m_tray->setStatus(QStringLiteral("NutSync"),
                      m_autoSyncPaused ? tr("自动同步已暂停") : tr("%1 个同步对").arg(enabled),
                      QStringLiteral("nutsync"));
    Q_UNUSED(enabled)
}

void MainWindow::scheduleTick()
{
    rebuildLocalMonitors();
    if (m_autoSyncPaused)
        return;
    const QDateTime now = QDateTime::currentDateTime();
    for (const SyncPair &pair : std::as_const(m_config.pairs)) {
        if (!pair.enabled || pair.intervalMinutes <= 0 || m_controller->contains(pair.id))
            continue;
        const bool due = !pair.lastSync.isValid()
            || pair.lastSync.secsTo(now) >= qint64(pair.intervalMinutes) * 60;
        if (due)
            requestSync(pair.id, false);
    }

    const QSet<QString> deferred = m_deferredAutoSyncs;
    for (const QString &id : deferred) {
        const SyncPair *pair = findPair(id);
        if (pair && pair->enabled && !m_controller->contains(id))
            requestSync(id, false);
    }
}

void MainWindow::syncStartupPairs()
{
    if (m_autoSyncPaused || !m_config.settings.autoSyncOnStartup)
        return;
    for (const SyncPair &pair : std::as_const(m_config.pairs)) {
        if (pair.enabled && pair.syncOnStartup && !m_controller->contains(pair.id))
            requestSync(pair.id, false);
    }
}

void MainWindow::setAutoSyncPaused(bool paused)
{
    if (m_autoSyncPaused == paused) {
        updateActions();
        return;
    }
    m_autoSyncPaused = paused;
    {
        const QSignalBlocker blocker(m_pauseAction);
        m_pauseAction->setChecked(paused);
    }
    if (m_tray)
        m_tray->setAutoSyncPaused(paused);
    appendSystemStatus(paused ? tr("自动同步已暂停") : tr("自动同步已恢复"));
    updateTrayIdleStatus();
    updateActions();
    if (!paused)
        QTimer::singleShot(0, this, &MainWindow::scheduleTick);
}

void MainWindow::openSettings()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("NutSync 首选项"));
    auto *outerLayout = new QVBoxLayout(&dialog);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);
    auto *scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(14);
    scroll->setWidget(content);
    outerLayout->addWidget(scroll, 1);

    auto *heading = new QLabel(tr("常规"), content);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    headingFont.setPointSize(headingFont.pointSize() + 2);
    heading->setFont(headingFont);
    layout->addWidget(heading);

    auto *languageRow = new QHBoxLayout();
    auto *languageLabel = new QLabel(tr("界面语言"), &dialog);
    auto *language = new QComboBox(&dialog);
    language->addItem(tr("跟随系统"), QStringLiteral("system"));
    language->addItem(QStringLiteral("English"), QStringLiteral("en"));
    language->addItem(QStringLiteral("简体中文"), QStringLiteral("zh_CN"));
    const int languageIndex = language->findData(m_config.settings.language);
    if (languageIndex >= 0)
        language->setCurrentIndex(languageIndex);
    languageRow->addWidget(languageLabel);
    languageRow->addWidget(language, 1);
    layout->addLayout(languageRow);
    auto *languageNote = new QLabel(tr("更改语言后需要重启 NutSync 才会生效。"), &dialog);
    languageNote->setWordWrap(true);
    makeMuted(languageNote, true);
    layout->addWidget(languageNote);

    auto *logLevelRow = new QHBoxLayout();
    auto *logLevelLabel = new QLabel(tr("日志级别"), &dialog);
    auto *logLevel = new QComboBox(&dialog);
    logLevel->addItem(tr("调试"), QStringLiteral("debug"));
    logLevel->addItem(tr("信息"), QStringLiteral("info"));
    logLevel->addItem(tr("警告"), QStringLiteral("warning"));
    logLevel->addItem(tr("错误"), QStringLiteral("error"));
    const int logLevelIndex = logLevel->findData(m_config.settings.logLevel);
    if (logLevelIndex >= 0)
        logLevel->setCurrentIndex(logLevelIndex);
    logLevelRow->addWidget(logLevelLabel);
    logLevelRow->addWidget(logLevel, 1);
    layout->addLayout(logLevelRow);

    auto *notifications = new QCheckBox(tr("显示桌面通知"), &dialog);
    notifications->setChecked(m_config.settings.notifications);
    auto *closeToTray = new QCheckBox(tr("关闭窗口时最小化到托盘"), &dialog);
    closeToTray->setChecked(m_config.settings.closeToTray);
    auto *autoStartup = new QCheckBox(tr("程序启动后同步标记为启动同步的同步对"), &dialog);
    autoStartup->setChecked(m_config.settings.autoSyncOnStartup);
    auto *startMinimized = new QCheckBox(tr("启动时隐藏主窗口（最小化到托盘）"), &dialog);
    startMinimized->setChecked(m_config.settings.startMinimized);
    if (!hasTray())
        startMinimized->setEnabled(false);

    AutostartManager autostartManager;
    auto *autostart = new QCheckBox(tr("登录时自动启动 NutSync（隐藏到系统托盘）"), &dialog);
    autostart->setChecked(autostartManager.isEnabled());
    autostart->setToolTip(tr("创建用户级 XDG autostart 条目，以 --hidden 参数启动；"
                             "如果系统托盘不可用，则仍会显示主窗口。"));

    layout->addWidget(notifications);
    layout->addWidget(closeToTray);
    layout->addWidget(autoStartup);
    layout->addWidget(startMinimized);
    layout->addWidget(autostart);

    auto *networkGroup = new QGroupBox(tr("网络"), &dialog);
    auto *networkForm = new QFormLayout(networkGroup);
    auto *proxyMode = new QComboBox(networkGroup);
    proxyMode->addItem(tr("使用系统代理"), QStringLiteral("system"));
    proxyMode->addItem(tr("不使用代理"), QStringLiteral("none"));
    proxyMode->addItem(tr("手动代理"), QStringLiteral("manual"));
    const int proxyIndex = proxyMode->findData(m_config.settings.proxyMode);
    if (proxyIndex >= 0)
        proxyMode->setCurrentIndex(proxyIndex);
    auto *proxyHost = new QLineEdit(m_config.settings.proxyHost, networkGroup);
    auto *proxyPort = new QSpinBox(networkGroup);
    proxyPort->setRange(1, 65535);
    proxyPort->setValue(m_config.settings.proxyPort);
    auto *proxyUser = new QLineEdit(m_config.settings.proxyUser, networkGroup);
    auto *proxyPassword = new QLineEdit(networkGroup);
    proxyPassword->setEchoMode(QLineEdit::Password);
    proxyPassword->setPlaceholderText(tr("留空则保留已保存的密码"));
    auto *clearProxyPassword = new QPushButton(tr("清除"), networkGroup);
    clearProxyPassword->setToolTip(tr("删除系统钥匙串中保存的代理密码"));
    auto *proxyPasswordRow = new QWidget(networkGroup);
    auto *proxyPasswordLayout = new QHBoxLayout(proxyPasswordRow);
    proxyPasswordLayout->setContentsMargins(0, 0, 0, 0);
    proxyPasswordLayout->addWidget(proxyPassword, 1);
    proxyPasswordLayout->addWidget(clearProxyPassword);
    auto *caPath = new QLineEdit(m_config.settings.caBundlePath, networkGroup);
    auto *browseCa = new QPushButton(tr("浏览…"), networkGroup);
    auto *caRow = new QWidget(networkGroup);
    auto *caLayout = new QHBoxLayout(caRow);
    caLayout->setContentsMargins(0, 0, 0, 0);
    caLayout->addWidget(caPath, 1);
    caLayout->addWidget(browseCa);

    networkForm->addRow(tr("代理模式"), proxyMode);
    networkForm->addRow(tr("主机"), proxyHost);
    networkForm->addRow(tr("端口"), proxyPort);
    networkForm->addRow(tr("代理用户名"), proxyUser);
    networkForm->addRow(tr("代理密码"), proxyPasswordRow);
    networkForm->addRow(tr("CA 证书文件"), caRow);
    auto *retryCount = new QSpinBox(networkGroup);
    retryCount->setRange(0, 5);
    retryCount->setValue(m_config.settings.retryCount);
    retryCount->setSpecialValueText(tr("不重试"));
    auto *retryDelay = new QSpinBox(networkGroup);
    retryDelay->setRange(5, 600);
    retryDelay->setSuffix(tr(" 秒"));
    retryDelay->setValue(m_config.settings.retryDelaySeconds);
    networkForm->addRow(tr("失败重试次数"), retryCount);
    networkForm->addRow(tr("重试基础间隔"), retryDelay);
    layout->addWidget(networkGroup);

    auto updateProxyFields = [=]() {
        const bool manual = proxyMode->currentData().toString() == QLatin1String("manual");
        proxyHost->setEnabled(manual);
        proxyPort->setEnabled(manual);
        proxyUser->setEnabled(manual);
        proxyPassword->setEnabled(manual);
    };
    connect(proxyMode, &QComboBox::currentIndexChanged, &dialog, [updateProxyFields](int) {
        updateProxyFields();
    });
    updateProxyFields();
    connect(clearProxyPassword, &QPushButton::clicked, &dialog, [&]() {
        QString error;
        if (!m_credentials.deletePassword(QStringLiteral("proxy-password"), &error)) {
            QMessageBox::warning(&dialog, tr("无法清除代理密码"), error);
            return;
        }
        proxyPassword->clear();
        proxyPassword->setPlaceholderText(tr("已清除"));
    });
    connect(browseCa, &QPushButton::clicked, &dialog, [&]() {
        const QString file =
            QFileDialog::getOpenFileName(&dialog, tr("选择 CA 证书文件"), caPath->text(),
                                         tr("证书文件 (*.pem *.crt *.cer);;所有文件 (*)"));
        if (!file.isEmpty())
            caPath->setText(file);
    });

    auto *autostartNote =
        new QLabel(tr("用户级 XDG autostart 条目：\n%1\n"
                      "仅对当前用户生效；取消勾选会删除该条目。")
                       .arg(PathUtils::nativePath(autostartManager.desktopFilePath())),
                   &dialog);
    autostartNote->setWordWrap(true);
    autostartNote->setTextInteractionFlags(Qt::TextSelectableByMouse);
    makeMuted(autostartNote, true);
    layout->addWidget(autostartNote);

    auto *note = new QLabel(tr("同步间隔和冲突策略按同步对单独设置。密码保存在系统的 KWallet / "
                               "Secret Service 中，不会写入配置文件。"),
                            &dialog);
    note->setWordWrap(true);
    makeMuted(note);
    layout->addWidget(note);

    auto *logLine = new QHBoxLayout();
    auto *logLabel =
        new QLabel(tr("日志：%1").arg(PathUtils::nativePath(Logger::logFilePath())), &dialog);
    logLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    logLabel->setWordWrap(true);
    auto *openLog = new QPushButton(tr("打开日志目录"), &dialog);
    connect(openLog, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::dataDir()));
    });
    logLine->addWidget(logLabel, 1);
    logLine->addWidget(openLog);
    layout->addLayout(logLine);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    outerLayout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (proxyMode->currentData().toString() == QLatin1String("manual")
            && proxyHost->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dialog, tr("代理配置不完整"),
                                 tr("手动代理模式需要填写代理主机。"));
            proxyHost->setFocus();
            return;
        }
        const QString caFile = caPath->text().trimmed();
        if (!caFile.isEmpty()
            && (!QFileInfo(caFile).isFile()
                || QSslCertificate::fromPath(caFile, QSsl::Pem).isEmpty())) {
            QMessageBox::warning(&dialog, tr("CA 证书无效"),
                                 tr("无法读取 CA 证书文件“%1”，请选择 PEM 证书。").arg(caFile));
            caPath->setFocus();
            return;
        }
        if (autostart->isChecked() != autostartManager.isEnabled()) {
            QString error;
            if (!autostartManager.setEnabled(autostart->isChecked(), &error)) {
                QMessageBox::warning(&dialog, tr("无法设置开机自启"), error);
                return;
            }
        }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    dialog.resize(560, 760);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString previousLanguage = m_config.settings.language;
    m_config.settings.language = language->currentData().toString();
    m_config.settings.proxyMode = proxyMode->currentData().toString();
    m_config.settings.proxyHost = proxyHost->text().trimmed();
    m_config.settings.proxyPort = proxyPort->value();
    m_config.settings.proxyUser = proxyUser->text().trimmed();
    m_config.settings.caBundlePath = caPath->text().trimmed();
    m_config.settings.logLevel = logLevel->currentData().toString();
    m_config.settings.retryCount = retryCount->value();
    m_config.settings.retryDelaySeconds = retryDelay->value();
    m_config.settings.notifications = notifications->isChecked();
    m_config.settings.closeToTray = closeToTray->isChecked();
    m_config.settings.autoSyncOnStartup = autoStartup->isChecked();
    m_config.settings.startMinimized = startMinimized->isChecked();
    if (m_tray)
        m_tray->setNotificationsEnabled(m_config.settings.notifications);
    if (!proxyPassword->text().isEmpty()) {
        QString passwordError;
        if (!m_credentials.writePassword(QStringLiteral("proxy-password"), proxyPassword->text(),
                                         &passwordError)) {
            QMessageBox::warning(this, tr("无法保存代理密码"), passwordError);
        }
    }
    Logger::setLevel(m_config.settings.logLevel);
    if (m_config.settings.retryCount == 0) {
        const auto ids = m_retryTimers.keys();
        for (const QString &id : ids)
            cancelRetry(id);
    }
    saveConfig(true);
    appendSystemStatus(tr("首选项已保存"));
    if (m_config.settings.language != previousLanguage) {
        QMessageBox::information(this, tr("语言已更改"),
                                 tr("界面语言将在下次启动 NutSync 时生效。"));
    }
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("关于 NutSync"),
                       tr("<h3>NutSync %1</h3>"
                          "<p>适用于 KDE Plasma 的 WebDAV 双向同步工具。</p>"
                          "<p>基于 Qt 6 / KDE Frameworks 构建。同步引擎采用三路状态比较："
                          "只有一侧发生变化时自动传播；两侧都发生变化时按策略处理；"
                          "默认保留双方副本。删除操作也会同步。</p>"
                          "<p>密码存储在系统 KWallet / Secret Service 中，配置文件不保存密码。</p>"
                          "<p style=\"color: gray;\">协议后端可扩展，目前提供 WebDAV 实现。</p>")
                           .arg(QString::fromLatin1(NUTSYNC_VERSION)));
}

void MainWindow::quitApplication()
{
    if (m_busy) {
        const auto answer = QMessageBox::question(
            this, tr("同步正在进行"), tr("仍有同步任务正在运行。确定要退出并中断同步吗？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
        m_controller->cancelCurrent();
    }
    saveConfig(false);
    m_quitting = true;
    qApp->quit();
}

void MainWindow::showAndRaise()
{
    if (isMinimized())
        showNormal();
    else
        show();
    raise();
    activateWindow();
}

void MainWindow::appendSystemStatus(const QString &message)
{
    if (m_globalStatus)
        m_globalStatus->setText(message);
    Logger::log(QStringLiteral("ui"), message);
}

void MainWindow::refreshChromePalette()
{
    const QPalette appPalette = QApplication::palette();
    menuBar()->setPalette(appPalette);
    const auto toolBars = findChildren<QToolBar *>();
    for (QToolBar *toolBar : toolBars) {
        toolBar->setPalette(appPalette);
        for (QToolButton *button : toolBar->findChildren<QToolButton *>())
            button->setPalette(appPalette);
    }
    const auto menus = menuBar()->findChildren<QMenu *>();
    for (QMenu *menu : menus)
        menu->setPalette(appPalette);
    if (m_tray)
        m_tray->refreshTheme();
    refreshTaskbarIcon();
}

void MainWindow::refreshTaskbarIcon()
{
    setWindowIcon(isMinimized() ? AppIcons::monochrome(AppIcons::isDarkTheme())
                                : AppIcons::colored());
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange)
        refreshTaskbarIcon();
    if (event->type() == QEvent::PaletteChange
        || event->type() == QEvent::ApplicationPaletteChange) {
        QTimer::singleShot(0, this, &MainWindow::refreshChromePalette);
    }
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    QTimer::singleShot(0, this, &MainWindow::refreshChromePalette);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_quitting) {
        event->accept();
        return;
    }

    if (hasTray() && m_config.settings.closeToTray) {
        hide();
        if (!m_trayHintShown && m_tray) {
            m_trayHintShown = true;
            m_tray->showNotification(tr("NutSync 仍在运行"),
                                     tr("主窗口已隐藏到系统托盘。同步会按计划继续进行。"), false);
        }
        event->ignore();
        return;
    }

    if (m_busy) {
        const auto answer = QMessageBox::question(
            this, tr("同步正在进行"), tr("仍有同步任务正在运行。确定要退出并中断同步吗？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_controller->cancelCurrent();
    }
    saveConfig(false);
    m_quitting = true;
    event->accept();
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

} // namespace NutSync
