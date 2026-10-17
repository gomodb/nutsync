#include "ui/pair_dialog.h"

#include "core/path_utils.h"
#include "core/webdav_backend.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QRegularExpression>
#include <QSpinBox>
#include <QWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace NutSync {

namespace {
void makeMuted(QLabel *label, bool smaller = false)
{
    label->setForegroundRole(QPalette::PlaceholderText);
    if (smaller) {
        QFont font = label->font();
        font.setPointSizeF(font.pointSizeF() * 0.9);
        label->setFont(font);
    }
}
} // namespace

PairDialog::PairDialog(const SyncPair &pair, bool credentialsAvailable, QWidget *parent)
    : QDialog(parent)
    , m_original(pair)
{
    buildUi(credentialsAvailable);

    m_name->setText(pair.name);
    m_localPath->setText(pair.localPath);
    m_remoteUrl->setText(pair.remoteUrl);
    m_username->setText(pair.username);
    m_password->setText(pair.password);
    m_rememberPassword->setChecked(pair.rememberPassword && credentialsAvailable);
    m_excludes->setPlainText(pair.excludePatterns.join(QLatin1Char('\n')));
    const int policyIndex = m_conflictPolicy->findData(static_cast<int>(pair.conflictPolicy));
    if (policyIndex >= 0)
        m_conflictPolicy->setCurrentIndex(policyIndex);
    m_interval->setValue(pair.intervalMinutes);
    m_rateLimitEnabled->setChecked(pair.rateLimitEnabled);
    const int windowMinutes = pair.rateLimitWindowMinutes > 0
        ? pair.rateLimitWindowMinutes : 60;
    if (windowMinutes % 60 == 0) {
        m_rateLimitUnit->setCurrentIndex(1);
        m_rateLimitWindow->setMaximum(168);
        m_rateLimitWindow->setValue(windowMinutes / 60);
    } else {
        m_rateLimitUnit->setCurrentIndex(0);
        m_rateLimitWindow->setMaximum(10080);
        m_rateLimitWindow->setValue(windowMinutes);
    }
    m_rateLimitCount->setValue(pair.rateLimitMaxRequests > 0
                                   ? pair.rateLimitMaxRequests : 10);
    m_rateLimitRow->setEnabled(pair.rateLimitEnabled);
    m_enabled->setChecked(pair.enabled);
    m_syncOnStartup->setChecked(pair.syncOnStartup);
    m_watchLocalChanges->setChecked(pair.watchLocalChanges);
    m_syncDeletions->setChecked(pair.syncDeletions);

    connect(&m_testWatcher, &QFutureWatcher<TestResult>::finished, this, [this]() {
        if (!m_testWatcher.isFinished())
            return;
        const TestResult result = m_testWatcher.result();
        m_testButton->setEnabled(true);
        if (result.first) {
            m_testStatus->setStyleSheet(QStringLiteral("color: #2e7d32;"));
            m_testStatus->setText(tr("连接成功，WebDAV 目录可访问。"));
        } else {
            m_testStatus->setStyleSheet(QStringLiteral("color: #c62828;"));
            m_testStatus->setText(tr("连接失败：%1").arg(result.second));
        }
    });
}

void PairDialog::buildUi(bool credentialsAvailable)
{
    const bool isNew = m_original.id.isEmpty();
    setWindowTitle(isNew ? tr("添加同步对") : tr("编辑同步对"));
    resize(700, 780);
    setMinimumWidth(620);
    setMinimumHeight(560);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);

    auto *title = new QLabel(isNew ? tr("新建 WebDAV 同步对") : tr("同步对设置"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 3);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto *intro = new QLabel(
        tr("本地文件夹与 WebDAV 目录将进行双向同步。删除操作也会同步；"
           "发生冲突时默认保留双方副本，不会静默丢弃数据。"),
        this);
    intro->setWordWrap(true);
    makeMuted(intro);
    layout->addWidget(intro);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *formHost = new QWidget(scroll);
    auto *form = new QFormLayout(formHost);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(10);

    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(tr("例如：Nextcloud 文档"));
    form->addRow(tr("名称"), m_name);

    auto *localRow = new QWidget(this);
    auto *localLayout = new QHBoxLayout(localRow);
    localLayout->setContentsMargins(0, 0, 0, 0);
    localLayout->setSpacing(8);
    m_localPath = new QLineEdit(localRow);
    m_localPath->setPlaceholderText(tr("选择要同步的本地文件夹"));
    m_localPath->setClearButtonEnabled(true);
    auto *browse = new QPushButton(tr("浏览…"), localRow);
    localLayout->addWidget(m_localPath, 1);
    localLayout->addWidget(browse);
    form->addRow(tr("本地文件夹"), localRow);

    m_remoteUrl = new QLineEdit(this);
    m_remoteUrl->setPlaceholderText(
        QStringLiteral("https://cloud.example.com/remote.php/dav/files/user/NutSync"));
    m_remoteUrl->setClearButtonEnabled(true);
    form->addRow(tr("WebDAV 地址"), m_remoteUrl);

    auto *remoteHint = new QLabel(
        tr("填写同步根目录的完整 URL，而不是服务器首页。程序会在此目录下递归同步。"),
        this);
    remoteHint->setWordWrap(true);
    makeMuted(remoteHint, true);
    form->addRow(QString(), remoteHint);

    m_username = new QLineEdit(this);
    m_username->setPlaceholderText(tr("留空表示匿名访问"));
    form->addRow(tr("用户名"), m_username);

    auto *passwordRow = new QWidget(this);
    auto *passwordLayout = new QHBoxLayout(passwordRow);
    passwordLayout->setContentsMargins(0, 0, 0, 0);
    passwordLayout->setSpacing(8);
    m_password = new QLineEdit(passwordRow);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(tr("WebDAV 密码或应用专用密码"));
    m_showPassword = new QCheckBox(tr("显示"), passwordRow);
    passwordLayout->addWidget(m_password, 1);
    passwordLayout->addWidget(m_showPassword);
    form->addRow(tr("密码"), passwordRow);

    m_rememberPassword = new QCheckBox(tr("使用系统钥匙串保存密码"), this);
    m_rememberPassword->setChecked(credentialsAvailable);
    if (!credentialsAvailable) {
        m_rememberPassword->setChecked(false);
        m_rememberPassword->setEnabled(false);
        m_rememberPassword->setToolTip(
            tr("此构建未检测到系统钥匙串支持，密码只能在本次运行期间使用。"));
    }
    form->addRow(QString(), m_rememberPassword);

    m_enabled = new QCheckBox(tr("启用此同步对"), this);
    m_enabled->setChecked(true);
    m_syncOnStartup = new QCheckBox(tr("程序启动后自动同步"), this);
    m_syncOnStartup->setChecked(true);
    m_watchLocalChanges = new QCheckBox(
        tr("监视本地文件变化并自动同步（inotify）"), this);
    m_watchLocalChanges->setChecked(true);
    m_syncDeletions = new QCheckBox(
        tr("同步删除：允许本地删除传播到远程"), this);
    m_syncDeletions->setChecked(false);
    m_syncDeletions->setToolTip(
        tr("关闭时，本地删除不会删除远程文件；下一次同步会从远程恢复该文件。"
           "远程删除始终会同步到本地。"));
    form->addRow(QString(), m_enabled);
    form->addRow(QString(), m_syncOnStartup);
    form->addRow(QString(), m_watchLocalChanges);
    form->addRow(QString(), m_syncDeletions);

    auto *deleteHint = new QLabel(
        tr("“同步删除”关闭时，本地删除只会影响本地副本；远程文件会在下一次同步时恢复。"
           "远程删除仍然会传播到本地。"),
        this);
    deleteHint->setWordWrap(true);
    makeMuted(deleteHint, true);
    form->addRow(QString(), deleteHint);

    m_excludes = new QPlainTextEdit(this);
    m_excludes->setPlaceholderText(
        tr("每行一个规则，例如：\n.git\n*.tmp\nnode_modules\n缓存/"));
    m_excludes->setFixedHeight(80);
    form->addRow(tr("排除规则"), m_excludes);

    m_conflictPolicy = new QComboBox(this);
    m_conflictPolicy->addItem(conflictPolicyDisplayName(ConflictPolicy::KeepBoth),
                              static_cast<int>(ConflictPolicy::KeepBoth));
    m_conflictPolicy->addItem(conflictPolicyDisplayName(ConflictPolicy::PreferLocal),
                              static_cast<int>(ConflictPolicy::PreferLocal));
    m_conflictPolicy->addItem(conflictPolicyDisplayName(ConflictPolicy::PreferRemote),
                              static_cast<int>(ConflictPolicy::PreferRemote));
    m_conflictPolicy->addItem(conflictPolicyDisplayName(ConflictPolicy::NewerWins),
                              static_cast<int>(ConflictPolicy::NewerWins));
    form->addRow(tr("冲突处理"), m_conflictPolicy);

    m_interval = new QSpinBox(this);
    m_interval->setRange(0, 10080);
    m_interval->setSpecialValueText(tr("不启用定时同步"));
    m_interval->setSuffix(tr(" 分钟"));
    m_interval->setValue(15);
    form->addRow(tr("定时同步间隔"), m_interval);

    m_rateLimitEnabled = new QCheckBox(
        tr("限制自动同步请求频率（默认不限制）"), this);
    form->addRow(QString(), m_rateLimitEnabled);

    m_rateLimitRow = new QWidget(this);
    auto *rateLimitLayout = new QHBoxLayout(m_rateLimitRow);
    rateLimitLayout->setContentsMargins(0, 0, 0, 0);
    rateLimitLayout->setSpacing(8);
    rateLimitLayout->addWidget(new QLabel(tr("在"), m_rateLimitRow));
    m_rateLimitWindow = new QSpinBox(m_rateLimitRow);
    m_rateLimitWindow->setRange(1, 10080);
    m_rateLimitWindow->setValue(1);
    rateLimitLayout->addWidget(m_rateLimitWindow);
    m_rateLimitUnit = new QComboBox(m_rateLimitRow);
    m_rateLimitUnit->addItem(tr("分钟"), 1);
    m_rateLimitUnit->addItem(tr("小时"), 60);
    m_rateLimitUnit->setCurrentIndex(1);
    rateLimitLayout->addWidget(m_rateLimitUnit);
    rateLimitLayout->addWidget(new QLabel(tr("内最多"), m_rateLimitRow));
    m_rateLimitCount = new QSpinBox(m_rateLimitRow);
    m_rateLimitCount->setRange(1, 100000);
    m_rateLimitCount->setValue(10);
    rateLimitLayout->addWidget(m_rateLimitCount);
    rateLimitLayout->addWidget(new QLabel(tr("次同步"), m_rateLimitRow));
    rateLimitLayout->addStretch(1);
    form->addRow(tr("请求频率上限"), m_rateLimitRow);

    auto *rateLimitHint = new QLabel(
        tr("限制定时同步、inotify 本地变更触发和启动同步在窗口内的启动次数。"
           "手动同步始终允许，但会计入额度。"),
        this);
    rateLimitHint->setWordWrap(true);
    makeMuted(rateLimitHint, true);
    form->addRow(QString(), rateLimitHint);

    scroll->setWidget(formHost);
    layout->addWidget(scroll, 1);

    auto *testRow = new QHBoxLayout();
    m_testButton = new QPushButton(QIcon::fromTheme(QStringLiteral("network-connect")),
                                   tr("测试连接"), this);
    m_testStatus = new QLabel(tr("保存前可先验证地址和凭据。"), this);
    m_testStatus->setWordWrap(true);
    m_testStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    testRow->addWidget(m_testButton);
    testRow->addWidget(m_testStatus, 1);
    layout->addLayout(testRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                         this);
    buttons->button(QDialogButtonBox::Save)->setText(tr("保存"));
    buttons->button(QDialogButtonBox::Save)->setIcon(
        QIcon::fromTheme(QStringLiteral("dialog-ok-apply")));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    layout->addWidget(buttons);

    connect(browse, &QPushButton::clicked, this, &PairDialog::browseLocalFolder);
    connect(m_showPassword, &QCheckBox::toggled, this, [this](bool shown) {
        m_password->setEchoMode(shown ? QLineEdit::Normal : QLineEdit::Password);
    });
    connect(m_rateLimitEnabled, &QCheckBox::toggled, m_rateLimitRow,
            &QWidget::setEnabled);
    connect(m_rateLimitUnit, &QComboBox::currentIndexChanged, this, [this]() {
        const int unit = m_rateLimitUnit->currentData().toInt();
        m_rateLimitWindow->setMaximum(unit == 60 ? 168 : 10080);
    });
    connect(m_testButton, &QPushButton::clicked, this, &PairDialog::testConnection);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (!validateInputs(false))
            return;
        QString localPath = m_localPath->text().trimmed();
        if (localPath.startsWith(QLatin1Char('~')))
            localPath = QDir::homePath() + localPath.mid(1);
        localPath = QDir::cleanPath(QFileInfo(localPath).absoluteFilePath());
        if (!QFileInfo(localPath).isDir()) {
            const auto answer = QMessageBox::question(
                this, tr("创建本地文件夹？"),
                tr("本地文件夹“%1”不存在。是否立即创建？").arg(PathUtils::nativePath(localPath)),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (answer != QMessageBox::Yes)
                return;
            if (!QDir().mkpath(localPath)) {
                QMessageBox::warning(this, tr("无法创建文件夹"),
                                     tr("无法创建本地文件夹：%1")
                                         .arg(PathUtils::nativePath(localPath)));
                return;
            }
        }
        m_localPath->setText(localPath);
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_name->setFocus();
}

void PairDialog::browseLocalFolder()
{
    QString start = m_localPath->text().trimmed();
    if (start.isEmpty() || !QFileInfo(start).isDir())
        start = QDir::homePath();
    const QString folder = QFileDialog::getExistingDirectory(
        this, tr("选择本地同步文件夹"), start,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!folder.isEmpty())
        m_localPath->setText(QDir::cleanPath(folder));
}

QUrl PairDialog::normalizedRemoteUrl(bool *ok) const
{
    if (ok)
        *ok = false;
    QString value = m_remoteUrl->text().trimmed();
    if (value.isEmpty())
        return {};
    if (!value.contains(QStringLiteral("://")))
        value.prepend(QStringLiteral("https://"));

    QUrl url(value, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty()
        || (url.scheme() != QLatin1String("http")
            && url.scheme() != QLatin1String("https"))) {
        return {};
    }
    QString path = url.path();
    if (path.isEmpty())
        path = QStringLiteral("/");
    if (!path.endsWith(QLatin1Char('/')))
        path.append(QLatin1Char('/'));
    url.setPath(path);
    url.setFragment({});
    if (ok)
        *ok = true;
    return url;
}

bool PairDialog::validateInputs(bool forTestOnly)
{
    if (m_name->text().trimmed().isEmpty() && !forTestOnly) {
        QMessageBox::warning(this, tr("信息不完整"), tr("请填写同步对名称。"));
        m_name->setFocus();
        return false;
    }
    if (m_localPath->text().trimmed().isEmpty() && !forTestOnly) {
        QMessageBox::warning(this, tr("信息不完整"), tr("请选择本地文件夹。"));
        m_localPath->setFocus();
        return false;
    }
    bool urlOk = false;
    const QUrl url = normalizedRemoteUrl(&urlOk);
    if (!urlOk) {
        QMessageBox::warning(
            this, tr("地址无效"),
            tr("请填写有效的 http:// 或 https:// WebDAV 目录地址。"));
        m_remoteUrl->setFocus();
        return false;
    }
    if (url.userName().isEmpty() && m_username->text().trimmed().isEmpty()
        && m_password->text().isEmpty() && !forTestOnly) {
        // Anonymous access is valid, so no warning here.
    }
    return true;
}

void PairDialog::testConnection()
{
    if (m_testWatcher.isRunning())
        return;
    if (!validateInputs(true))
        return;

    bool ok = false;
    const QUrl url = normalizedRemoteUrl(&ok);
    if (!ok)
        return;

    WebDavOptions options;
    options.baseUrl = url;
    options.username = m_username->text().trimmed();
    options.password = m_password->text();
    if (options.username.isEmpty() && !url.userName().isEmpty()) {
        options.username = url.userName();
        options.password = url.password();
    }

    m_testButton->setEnabled(false);
    m_testStatus->setStyleSheet(QString());
    m_testStatus->setText(tr("正在连接 WebDAV 服务器…"));

    m_testWatcher.setFuture(QtConcurrent::run([options]() -> TestResult {
        WebDavBackend backend(options);
        QString error;
        const bool success = backend.testConnection(&error);
        return qMakePair(success, error);
    }));
}

QStringList PairDialog::parsedExcludes() const
{
    const QStringList raw = m_excludes->toPlainText().split(
        QRegularExpression(QStringLiteral("[,;\\n]")), Qt::SkipEmptyParts);
    QStringList result;
    for (QString line : raw) {
        line = line.trimmed();
        if (!line.isEmpty() && !result.contains(line))
            result.append(line);
    }
    return result;
}

SyncPair PairDialog::syncPair() const
{
    SyncPair pair = m_original;
    if (pair.id.isEmpty())
        pair.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    pair.name = m_name->text().trimmed();
    if (pair.name.isEmpty())
        pair.name = QFileInfo(m_localPath->text()).fileName();
    QString localPath = m_localPath->text().trimmed();
    if (localPath.startsWith(QLatin1Char('~')))
        localPath = QDir::homePath() + localPath.mid(1);
    pair.localPath = QDir::cleanPath(QFileInfo(localPath).absoluteFilePath());
    bool ok = false;
    QUrl url = normalizedRemoteUrl(&ok);
    pair.remoteUrl = ok ? url.toString() : m_remoteUrl->text().trimmed();
    pair.username = m_username->text().trimmed();
    pair.password = m_password->text();
    if (ok && (!url.userName().isEmpty() || !url.password().isEmpty())) {
        if (pair.username.isEmpty())
            pair.username = url.userName();
        if (pair.password.isEmpty())
            pair.password = url.password();
        url.setUserInfo({});
        pair.remoteUrl = url.toString();
    }
    pair.rememberPassword = m_rememberPassword->isChecked();
    pair.excludePatterns = parsedExcludes();
    pair.conflictPolicy = static_cast<ConflictPolicy>(
        m_conflictPolicy->currentData().toInt());
    pair.intervalMinutes = m_interval->value();
    pair.rateLimitEnabled = m_rateLimitEnabled->isChecked();
    pair.rateLimitWindowMinutes = m_rateLimitWindow->value()
        * m_rateLimitUnit->currentData().toInt();
    pair.rateLimitMaxRequests = m_rateLimitCount->value();
    if (pair.rateLimitWindowMinutes <= 0)
        pair.rateLimitWindowMinutes = 60;
    if (pair.rateLimitMaxRequests <= 0)
        pair.rateLimitMaxRequests = 10;
    pair.enabled = m_enabled->isChecked();
    pair.syncOnStartup = m_syncOnStartup->isChecked();
    pair.watchLocalChanges = m_watchLocalChanges->isChecked();
    pair.syncDeletions = m_syncDeletions->isChecked();
    return pair;
}

} // namespace NutSync
