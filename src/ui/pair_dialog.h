#pragma once

#include "core/sync_types.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QPair>
#include <QUrl>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QWidget;

namespace NutSync {

class PairDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PairDialog(const SyncPair &pair, bool credentialsAvailable, QWidget *parent = nullptr);

    SyncPair syncPair() const;

private:
    using TestResult = QPair<bool, QString>;

    void buildUi(bool credentialsAvailable);
    void browseLocalFolder();
    void testConnection();
    bool validateInputs(bool forTestOnly);
    QUrl normalizedRemoteUrl(bool *ok) const;
    QStringList parsedExcludes() const;

    SyncPair m_original;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_localPath = nullptr;
    QLineEdit *m_remoteUrl = nullptr;
    QLineEdit *m_username = nullptr;
    QLineEdit *m_password = nullptr;
    QCheckBox *m_showPassword = nullptr;
    QCheckBox *m_rememberPassword = nullptr;
    QPlainTextEdit *m_excludes = nullptr;
    QComboBox *m_conflictPolicy = nullptr;
    QSpinBox *m_interval = nullptr;
    QCheckBox *m_rateLimitEnabled = nullptr;
    QWidget *m_rateLimitRow = nullptr;
    QSpinBox *m_rateLimitWindow = nullptr;
    QComboBox *m_rateLimitUnit = nullptr;
    QSpinBox *m_rateLimitCount = nullptr;
    QCheckBox *m_enabled = nullptr;
    QCheckBox *m_syncOnStartup = nullptr;
    QCheckBox *m_watchLocalChanges = nullptr;
    QCheckBox *m_syncDeletions = nullptr;
    QPushButton *m_testButton = nullptr;
    QLabel *m_testStatus = nullptr;
    QFutureWatcher<TestResult> m_testWatcher;
};

} // namespace NutSync
