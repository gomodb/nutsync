#include "ui/sync_controller.h"

#include "core/sync_engine.h"

#include <QFutureWatcher>
#include <QTimer>

#include <utility>
#include <QtConcurrent>

namespace NutSync {

SyncController::SyncController(QString stateDatabasePath, QObject *parent)
    : QObject(parent)
    , m_stateDatabasePath(std::move(stateDatabasePath))
{
    connect(&m_watcher, &QFutureWatcher<void>::finished, this, &SyncController::onWorkerFinished);
}

SyncController::~SyncController()
{
    if (m_engine) {
        m_engine->requestCancel();
        // The controller is going away. Detach the engine so a still-running
        // worker does not access a destroyed controller during shutdown.
        m_watcher.disconnect();
        m_engine->setParent(nullptr);
    }
}

void SyncController::setSettings(const AppSettings &settings)
{
    m_settings = settings;
}

void SyncController::enqueue(const SyncPair &pair)
{
    if (!pair.enabled || pair.id.isEmpty())
        return;
    if (m_busy && m_currentPairId == pair.id)
        return;
    for (const SyncPair &queued : std::as_const(m_queue)) {
        if (queued.id == pair.id)
            return;
    }

    SyncPair copy = pair;
    if (copy.rememberPassword && copy.password.isEmpty()) {
        QString error;
        copy.password = m_credentials.readPassword(copy.id, &error);
        if (!error.isEmpty())
            qWarning().noquote() << "[sync-controller]" << error;
    }
    if (copy.name.isEmpty())
        copy.name = copy.localPath;
    m_queue.enqueue(copy);
    emit queueChanged(m_queue.size());
    if (!m_busy)
        QTimer::singleShot(0, this, &SyncController::startNext);
}

bool SyncController::isBusy() const
{
    return m_busy;
}

bool SyncController::contains(const QString &pairId) const
{
    if (m_busy && m_currentPairId == pairId)
        return true;
    for (const SyncPair &queued : m_queue) {
        if (queued.id == pairId)
            return true;
    }
    return false;
}

QString SyncController::currentPairId() const
{
    return m_busy ? m_currentPairId : QString();
}

int SyncController::pendingCount() const
{
    return m_queue.size();
}

void SyncController::cancelCurrent()
{
    if (m_engine)
        m_engine->requestCancel();
    clearQueue();
}

void SyncController::clearQueue()
{
    if (m_queue.isEmpty())
        return;
    m_queue.clear();
    emit queueChanged(0);
}

void SyncController::startNext()
{
    if (m_busy || m_queue.isEmpty())
        return;

    m_currentPair = m_queue.dequeue();
    m_currentPairId = m_currentPair.id;
    m_busy = true;
    m_hasPendingSummary = false;
    m_pendingSummary = {};

    emit busyChanged(true);
    emit queueChanged(m_queue.size());
    emit pairStarted(m_currentPairId);

    auto *engine = new SyncEngine(m_currentPair, m_stateDatabasePath, this);
    engine->setRetryPolicy(m_settings.retryCount, m_settings.retryDelaySeconds * 1000);
    m_engine = engine;
    connect(engine, &SyncEngine::logMessage, this, [this](const QString &message) {
        emit logMessage(m_currentPairId, message);
    });
    connect(engine, &SyncEngine::progress, this,
            [this](int completed, int total, const QString &path) {
                emit progress(m_currentPairId, completed, total, path);
            });
    connect(engine, &SyncEngine::finished, this, &SyncController::onEngineFinished);

    m_watcher.setFuture(QtConcurrent::run([engine]() {
        engine->run();
    }));
}

void SyncController::onEngineFinished(const SyncSummary &summary)
{
    m_pendingSummary = summary;
    m_hasPendingSummary = true;
}

void SyncController::onWorkerFinished()
{
    SyncEngine *engine = m_engine;
    SyncSummary summary = m_pendingSummary;
    if (!m_hasPendingSummary) {
        summary = engine ? engine->lastSummary() : SyncSummary{};
    }
    if (summary.pairId.isEmpty()) {
        summary.pairId = m_currentPairId;
        summary.pairName = m_currentPair.name;
        summary.success = false;
        summary.message = tr("同步工作线程异常结束");
        summary.errors = 1;
        summary.errorDetails.append(summary.message);
    }
    m_engine = nullptr;
    if (engine) {
        engine->deleteLater();
    }

    m_busy = false;
    m_currentPairId.clear();
    m_currentPair = {};
    m_hasPendingSummary = false;
    m_pendingSummary = {};

    emit pairFinished(summary);
    emit busyChanged(false);
    emit queueChanged(m_queue.size());

    if (!m_queue.isEmpty())
        QTimer::singleShot(0, this, &SyncController::startNext);
}

} // namespace NutSync
