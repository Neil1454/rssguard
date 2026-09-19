// For license of this file, see <project-root-folder>/LICENSE.md.

#include "torrent/torrentautomationengine.h"

#include "core/message.h"
#include "core/messagesmodel.h"
#include "miscellaneous/application.h"
#include "miscellaneous/feedreader.h"
#include "miscellaneous/settings.h"
#include "miscellaneous/notification.h"
#include "services/abstract/feed.h"
#include "torrent/torrentextractor.h"
#include "torrent/torrentsendhistory.h"

#include <QCryptographicHash>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QSystemTrayIcon>
#include <QTime>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <cmath>

#include <algorithm>
#include <limits>
#include <utility>

namespace {
  const QString RuntimeGroup = QStringLiteral("TorrentAutomation");
  const QString RuntimeKey = QStringLiteral("runtime");
  const QString AutomationTag = QStringLiteral("rssguard-auto");
  QPointer<TorrentAutomationEngine> s_engine;

  qint64 torrentSizeHint(const QString& url, qint64 fallback) {
    if (!url.startsWith(QStringLiteral("magnet:"), Qt::CaseInsensitive)) return fallback;
    bool ok = false;
    const qint64 exactLength = QUrlQuery(QUrl(url)).queryItemValue(QStringLiteral("xl")).toLongLong(&ok);
    return ok && exactLength > 0 ? exactLength : fallback;
  }

  QString magnetInfoHash(const QString& url) {
    if (!url.startsWith(QStringLiteral("magnet:"), Qt::CaseInsensitive)) return {};
    const QString xt = QUrlQuery(QUrl(url)).queryItemValue(QStringLiteral("xt"));
    const QString prefix = QStringLiteral("urn:btih:");
    return xt.startsWith(prefix, Qt::CaseInsensitive) ? xt.mid(prefix.size()).toLower() : QString();
  }
}

TorrentAutomationEngine* TorrentAutomationEngine::instance(QObject* parent) {
  if (s_engine.isNull()) s_engine = new TorrentAutomationEngine(parent == nullptr ? qApp : parent);
  return s_engine;
}

void TorrentAutomationEngine::processNewArticles(const QHash<Feed*, QList<Message>>& articles, QObject* parent) {
  TorrentAutomationEngine* engine = instance(parent);
  engine->m_lastArticles = articles;
  engine->enqueue(articles);
}

bool TorrentAutomationEngine::paused() const {
  return TorrentAutomationConfig::load(qApp->settings()).paused;
}

void TorrentAutomationEngine::setPaused(bool pausedState) {
  TorrentAutomationConfig config = TorrentAutomationConfig::load(qApp->settings());
  config.paused = pausedState;
  config.save(qApp->settings());
  m_config.paused = pausedState;
  Job event;
  event.title = pausedState ? tr("Automation paused") : tr("Automation resumed");
  record(pausedState ? QStringLiteral("paused") : QStringLiteral("resumed"), event, {},
         pausedState ? tr("New automatic sends, cleanup, retention and retries are held. Existing client transfers continue.")
                     : tr("Queued work will be checked again against current limits and live client status."));
  if (!pausedState && !m_deferredJobs.isEmpty() && !m_busy) {
    for (Job job : std::as_const(m_deferredJobs)) {
      job.nextAttempt = {};
      m_jobs.enqueue(job);
    }
    m_deferredJobs.clear();
    saveRuntime();
    beginBatch();
  }
}

QString TorrentAutomationEngine::storageOverview() const {
  QStringList lines;
  for (int index = 0; index < m_clients.size(); ++index) {
    const TorrentClientStatus status = index < m_statuses.size() ? m_statuses.at(index) : TorrentClientStatus();
    const TorrentAutomationClientPolicy policy = m_config.policyFor(m_clients.at(index).id);
    if (status.freeBytes >= 0) {
      lines.append(tr("%1: %2 GiB available%3")
        .arg(m_clients.at(index).name)
        .arg(status.freeBytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1)
        .arg(status.liveSpace ? tr(" (live)") : tr(" (estimated)")));
    }
    else if (policy.configuredCapacityBytes > 0)
      lines.append(tr("%1: capacity configured; available space awaits a status check").arg(m_clients.at(index).name));
    else lines.append(tr("%1: available space unavailable").arg(m_clients.at(index).name));
  }
  return lines.isEmpty() ? tr("Storage: status not checked yet") : lines.join(QLatin1Char('\n'));
}

void TorrentAutomationEngine::processApprovedArticles(Feed* feed,
                                                       const QList<Message>& articles,
                                                       QWidget* dialogParent,
                                                       QObject* parent) {
  if (articles.isEmpty()) return;
  TorrentAutomationEngine* engine = instance(parent);
  engine->m_manualDialogParent = dialogParent;
  engine->enqueue(QHash<Feed*, QList<Message>>{{feed, articles}}, false, true, true);
}

void TorrentAutomationEngine::processDirectArticles(const TorrentClientConfig& client,
                                                     const QList<Message>& articles,
                                                     QObject* parent) {
  if (articles.isEmpty() || client.id.isEmpty()) return;
  TorrentAutomationEngine* engine = instance(parent);
  if (engine->m_busy) {
    connect(engine, &TorrentAutomationEngine::busyChanged, engine,
            [client, articles, parent](bool busy) {
              if (!busy) TorrentAutomationEngine::processDirectArticles(client, articles, parent);
            }, Qt::SingleShotConnection);
    return;
  }
  engine->m_config = TorrentAutomationConfig::load(qApp->settings());
  for (const Message& message : articles) {
    for (const QString& url : TorrentExtractor::extract(message)) {
      if (TorrentSendHistory::instance(qApp)->wasSent(message.m_id, client.id)) continue;
      Job job;
      job.key = engine->jobKey(url);
      job.title = message.m_title;
      job.url = url;
      job.feedId = message.m_feedCustomId;
      job.messageId = message.m_id;
      job.allowedClientIds = {client.id};
      job.sizeBytes = torrentSizeHint(url, engine->m_config.unknownTorrentSizeBytes);
      job.directOverride = true;
      engine->m_jobs.enqueue(job);
    }
  }
  if (!engine->m_jobs.isEmpty() && !engine->m_busy) engine->beginBatch();
}

void TorrentAutomationEngine::runDryTest() {
  if (m_busy) {
    notify(tr("Torrent automation dry run"), tr("Automation is already processing another batch."), true);
    return;
  }
  m_config = TorrentAutomationConfig::load(qApp->settings());
  const auto queueRetentionTest = [this]() {
    if (!m_config.cleanupEnabled || !m_config.maximumRetentionEnabled) return;
    Job retention;
    retention.title = tr("Scheduled retention cleanup");
    retention.ruleName = tr("Maximum retention deadline");
    retention.retentionCleanup = true;
    m_jobs.enqueue(retention);
  };
  if (m_lastArticles.isEmpty()) {
    QHash<Feed*, QList<Message>> historical;
    MessagesModel* model = qApp->feedReader() == nullptr ? nullptr : qApp->feedReader()->messagesModel();
    if (model != nullptr) {
      QList<Message> fallback;
      for (const Message& message : std::as_const(model->messages())) {
        if (fallback.size() < 25) fallback.append(message);
        if (TorrentExtractor::extract(message).isEmpty()) continue;
        Feed* feed = model->feedById(message.m_feedId);
        historical[feed].append(message);
        int selected = 0;
        for (auto it = historical.constBegin(); it != historical.constEnd(); ++it) selected += it.value().size();
        if (selected >= 25) break;
      }
      if (historical.isEmpty()) {
        for (const Message& message : std::as_const(fallback))
          historical[model->feedById(message.m_feedId)].append(message);
      }
    }
    if (!historical.isEmpty()) {
      Job summary;
      summary.title = tr("Historical RSS sample");
      int count = 0;
      for (auto it = historical.constBegin(); it != historical.constEnd(); ++it) count += it.value().size();
      record(QStringLiteral("DRY RUN — HISTORICAL SAMPLE"), summary, {},
             tr("No newly fetched items were available, so this test is using %1 previously received RSS item(s). Already processed items may be included for simulation only; nothing will be sent, removed, re-queued or re-marked.").arg(count));
      queueRetentionTest();
      enqueue(historical, true);
      return;
    }
    if (m_config.cleanupEnabled && m_config.maximumRetentionEnabled) {
      m_forcedDryRun = true;
      queueRetentionTest();
      beginBatch();
      return;
    }
    Job summary;
    summary.title = tr("Manual dry run");
    record(QStringLiteral("DRY RUN"), summary, {},
           tr("No newly fetched or previously loaded RSS items are available. Open a feed containing articles, then run the test again."));
    return;
  }
  queueRetentionTest();
  enqueue(m_lastArticles, true);
}

TorrentAutomationEngine::TorrentAutomationEngine(QObject* parent) : QObject(parent) {
  loadRuntime();
  auto* maintenanceTimer = new QTimer(this);
  maintenanceTimer->setInterval(60 * 1000);
  connect(maintenanceTimer, &QTimer::timeout, this, [this]() {
    if (m_busy) return;
    m_config = TorrentAutomationConfig::load(qApp->settings());
    if (!m_config.enabled) return;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const bool reconciliationDue = !m_config.dryRun && m_config.reconciliationEnabled &&
      (!m_lastReconcile.isValid() || m_lastReconcile.secsTo(now) >= qMax(1, m_config.reconciliationMinutes) * 60);
    const bool retentionDue = !m_config.dryRun && m_config.cleanupEnabled && m_config.maximumRetentionEnabled &&
      (!m_lastRetentionCheck.isValid() || m_lastRetentionCheck.secsTo(now) >= 5 * 60);
    if (retentionDue) {
      Job retention;
      retention.title = tr("Scheduled retention cleanup");
      retention.ruleName = tr("Maximum retention deadline");
      retention.retentionCleanup = true;
      m_jobs.enqueue(retention);
    }
    if (reconciliationDue || retentionDue) beginBatch();
  });
  maintenanceTimer->start();
}

bool TorrentAutomationEngine::busy() const { return m_busy; }

QStringList TorrentAutomationEngine::recentActivity() const {
  QStringList result;
  for (int index = m_history.size() - 1; index >= 0; --index) {
    const QJsonObject item = m_history.at(index).toObject();
    result.append(QStringLiteral("%1 — %2 — %3")
                    .arg(item.value(QStringLiteral("time")).toString(),
                         item.value(QStringLiteral("state")).toString(),
                         item.value(QStringLiteral("detail")).toString()));
    if (result.size() >= qBound(50, m_config.historyLimit, 5000)) break;
  }
  return result;
}

QJsonArray TorrentAutomationEngine::activityHistory() const { return m_history; }

QStringList TorrentAutomationEngine::pendingRetries() const {
  QStringList result;
  for (const Job& job : m_deferredJobs) {
    result.append(tr("%1 — %2 — retry %3 — %4")
                    .arg(job.title.isEmpty() ? job.url : job.title)
                    .arg(job.queueReason.isEmpty() ? tr("Waiting") : job.queueReason)
                    .arg(job.attempt)
                    .arg(job.nextAttempt.toLocalTime().toString(QStringLiteral("dd/MM/yyyy HH:mm:ss"))));
  }
  return result;
}

void TorrentAutomationEngine::retryPending(int index) {
  if (index < 0 || index >= m_deferredJobs.size()) return;
  Job job = m_deferredJobs.takeAt(index);
  job.nextAttempt = {};
  m_jobs.enqueue(job);
  saveRuntime();
  if (!m_busy) beginBatch();
}

void TorrentAutomationEngine::retryAllPending() {
  if (m_deferredJobs.isEmpty()) return;
  const QList<Job> pending = m_deferredJobs;
  m_deferredJobs.clear();
  for (Job job : pending) {
    job.nextAttempt = {};
    job.queueReason.clear();
    m_jobs.enqueue(job);
  }
  saveRuntime();
  if (!m_busy) beginBatch();
}

void TorrentAutomationEngine::sendPendingToClient(int index, const QString& clientId) {
  if (index < 0 || index >= m_deferredJobs.size() || clientId.isEmpty()) return;
  Job job = m_deferredJobs.takeAt(index);
  job.allowedClientIds = {clientId};
  job.attemptedClientIds.clear();
  job.verificationClientId.clear();
  job.nextAttempt = {};
  job.queueReason.clear();
  job.directOverride = true;
  m_jobs.enqueue(job);
  saveRuntime();
  if (!m_busy) beginBatch();
}

void TorrentAutomationEngine::cancelPending(int index) {
  if (index < 0 || index >= m_deferredJobs.size()) return;
  const Job job = m_deferredJobs.takeAt(index);
  record(QStringLiteral("cancelled"), job, {}, tr("Queued retry cancelled by the user."));
  saveRuntime();
}

void TorrentAutomationEngine::cancelAllPending() {
  if (m_deferredJobs.isEmpty()) return;
  const QList<Job> pending = m_deferredJobs;
  m_deferredJobs.clear();
  for (const Job& job : pending)
    record(QStringLiteral("cancelled"), job, {}, tr("Queued retry cancelled by the user."));
  saveRuntime();
}

void TorrentAutomationEngine::clearActivityHistory() {
  m_history = {};
  saveRuntime();
  emit activityAdded(QString());
}

QString TorrentAutomationEngine::jobKey(const QString& url) const {
  QString normalized = url.trimmed();
  if (normalized.startsWith(QStringLiteral("magnet:"), Qt::CaseInsensitive)) normalized = normalized.toLower();
  return QString::fromLatin1(QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool TorrentAutomationEngine::ruleMatches(const TorrentAutomationRule& rule,
                                           const QString& feedId,
                                           const Message& message,
                                           qint64 torrentSize) const {
  if (!rule.enabled || (!rule.feedIds.isEmpty() && !rule.feedIds.contains(feedId))) return false;
  const QString searchable = message.m_title + QLatin1Char('\n') + message.m_contents;
  if (!rule.requiredText.isEmpty() && !searchable.contains(rule.requiredText, Qt::CaseInsensitive)) return false;
  if (!rule.excludedText.isEmpty() && searchable.contains(rule.excludedText, Qt::CaseInsensitive)) return false;
  if (!rule.titleRegularExpression.isEmpty()) {
    const QRegularExpression expression(rule.titleRegularExpression, QRegularExpression::CaseInsensitiveOption);
    if (!expression.isValid() || !expression.match(message.m_title).hasMatch()) return false;
  }
  if (rule.minimumSizeBytes > 0 && torrentSize < rule.minimumSizeBytes) return false;
  if (rule.maximumSizeBytes > 0 && torrentSize > rule.maximumSizeBytes) return false;
  return true;
}

void TorrentAutomationEngine::enqueue(const QHash<Feed*, QList<Message>>& articles,
                                      bool forceDryRun,
                                      bool manualApproval,
                                      bool forceEnabled) {
  m_config = TorrentAutomationConfig::load(qApp->settings());
  m_forcedDryRun = forceDryRun;
  if (forceDryRun) {
    m_config.enabled = true;
    m_config.dryRun = true;
  }
  if (!m_config.enabled && !forceEnabled) return;

  for (auto feedIt = articles.constBegin(); feedIt != articles.constEnd(); ++feedIt) {
    const QString feedId = feedIt.key() == nullptr ? QString() : feedIt.key()->customId();
    for (const Message& message : feedIt.value()) {
      const QString messageFeedId = feedId.isEmpty() ? message.m_feedCustomId : feedId;
      if (manualApproval && TorrentSendHistory::instance(qApp)->wasSentToAnyClient(message.m_id)) continue;
      const QStringList extracted = TorrentExtractor::extract(message);
      if (forceDryRun && extracted.isEmpty()) {
        Job skipped;
        skipped.title = message.m_title;
        skipped.feedId = messageFeedId;
        record(QStringLiteral("DRY RUN — NO TORRENT"), skipped, {},
               tr("The RSS item contained no supported magnet link or torrent URL, so automation would take no action."));
      }
      for (const QString& url : extracted) {
        const qint64 size = torrentSizeHint(url, m_config.unknownTorrentSizeBytes);
        QStringList allowed;
        bool matched = m_config.rules.isEmpty();
        QString matchedRule = m_config.rules.isEmpty() ? tr("No rules configured") : QString();
        for (const TorrentAutomationRule& rule : std::as_const(m_config.rules)) {
          if (ruleMatches(rule, messageFeedId, message, size)) {
            matched = true;
            allowed = rule.clientIds;
            matchedRule = rule.name;
            break;
          }
        }
        Job job;
        job.key = jobKey(url);
        job.title = message.m_title;
        job.url = url;
        job.feedId = messageFeedId;
        job.ruleName = matchedRule;
        job.messageId = message.m_id;
        job.allowedClientIds = allowed;
        job.sizeBytes = size;
        job.manualApproval = manualApproval;
        if (!matched) {
          if (forceDryRun)
            record(QStringLiteral("DRY RUN — NO RULE"), job, {},
                   tr("No enabled RSS automation rule matched this torrent, so it would not be routed, queued or cleaned up."));
          continue;
        }
        if (forceDryRun || !wasProcessed(job.key)) {
          if (m_config.paused && !manualApproval && !forceDryRun) {
            job.nextAttempt = QDateTime::currentDateTimeUtc().addSecs(60);
            job.queueReason = tr("Automation is paused");
            m_deferredJobs.append(job);
            record(QStringLiteral("paused"), job, {}, tr("Held safely until automation is resumed."));
          }
          else m_jobs.enqueue(job);
        }
      }
    }
  }
  if (m_config.paused && !m_deferredJobs.isEmpty()) saveRuntime();
  if (!m_jobs.isEmpty() && !m_busy) beginBatch();
  else if (forceDryRun && !m_busy) {
    Job summary;
    summary.title = tr("Manual dry run");
    record(QStringLiteral("DRY RUN"), summary, {},
           tr("No torrent items were available for routing simulation. Review the preceding dry-run entries for unmatched rules or RSS items without torrent links."));
    m_forcedDryRun = false;
  }
  else if (manualApproval && !m_busy) {
    Job summary;
    summary.title = tr("Manual processing");
    record(QStringLiteral("skipped"), summary, {},
           tr("No unprocessed torrent item matched the current RSS automation rules."));
    notify(tr("Torrent automation"), tr("No unprocessed selected item matched the current RSS automation rules."), true);
    emit busyChanged(false);
  }
}

void TorrentAutomationEngine::beginBatch() {
  m_busy = true;
  emit busyChanged(true);
  m_config = TorrentAutomationConfig::load(qApp->settings());
  if (m_forcedDryRun) m_config.dryRun = true;
  m_clients.clear();
  QStringList forcedClientIds;
  for (const Job& job : std::as_const(m_jobs))
    if (job.directOverride) forcedClientIds.append(job.allowedClientIds);
  for (TorrentClientConfig client : TorrentClientConfig::enabledInPriorityOrder(TorrentClientConfig::load(qApp->settings()))) {
    const TorrentAutomationClientPolicy policy = m_config.policyFor(client.id);
    if (policy.enabled || forcedClientIds.contains(client.id)) {
      client.requestTimeoutSeconds = policy.requestTimeoutSeconds > 0
                                       ? policy.requestTimeoutSeconds
                                       : m_config.requestTimeoutSeconds;
      m_clients.append(client);
    }
  }
  m_statuses.clear();
  m_statuses.resize(m_clients.size());
  m_pendingStatusQueries = m_clients.size();
  m_cleanupCount = 0;
  if (m_clients.isEmpty()) {
    notify(tr("Torrent automation"), tr("No enabled torrent clients participate in automation."), true);
    finishBatch();
    return;
  }
  for (int index = 0; index < m_clients.size(); ++index) queryClientStatus(index, 0);
}

void TorrentAutomationEngine::queryClientStatus(int index, int attempt) {
  if (attempt == 0) {
    QString breakerDetail;
    if (circuitBreakerOpen(m_clients.at(index).id, &breakerDetail)) {
      TorrentClientStatus status;
      status.detail = breakerDetail;
      m_statuses[index] = status;
      if (--m_pendingStatusQueries == 0) {
        if (!m_config.dryRun) reconcileManagedState();
        processNextJob();
      }
      return;
    }
  }
  TorrentClient* client = TorrentClient::create(m_clients.at(index), this);
  connect(client, &TorrentClient::statusFinished, this, [this, client, index, attempt](TorrentClientStatus status) {
    const TorrentAutomationClientPolicy policy = m_config.policyFor(m_clients.at(index).id);
    const int maximumRetries = policy.retryAttempts >= 0 ? policy.retryAttempts : m_config.retryAttempts;
    if (!status.reachable && m_config.retryEnabled && attempt < maximumRetries &&
        isTransientFailure(status.detail)) {
      if (m_config.dryRun) {
        qint64 delay = qMax(1, m_config.retryInitialSeconds);
        if (m_config.retryExponentialBackoff) delay *= (1LL << qMin(attempt, 16));
        delay = qMin<qint64>(delay, qMax(1, m_config.retryMaximumSeconds));
        Job statusJob;
        statusJob.title = m_clients.at(index).name;
        record(QStringLiteral("DRY RUN — STATUS RETRY"), statusJob, m_clients.at(index).id,
               tr("The status check failed temporarily. Live automation would retry attempt %1 of %2 after %3 seconds; the dry run did not wait or change client-health state. %4")
                 .arg(attempt + 1).arg(maximumRetries).arg(delay).arg(status.detail));
      }
      else {
      qint64 delay = qMax(1, m_config.retryInitialSeconds);
      if (m_config.retryExponentialBackoff) delay *= (1LL << qMin(attempt, 16));
      delay = qMin<qint64>(delay, qMax(1, m_config.retryMaximumSeconds));
      Job statusJob;
      statusJob.title = m_clients.at(index).name;
      record(QStringLiteral("status-retry"), statusJob, m_clients.at(index).id,
             tr("Status check failed; retry %1 of %2 in %3 seconds. %4")
               .arg(attempt + 1).arg(maximumRetries).arg(delay).arg(status.detail));
      client->deleteLater();
      QTimer::singleShot(int(qMin<qint64>(delay * 1000, std::numeric_limits<int>::max())), this,
                         [this, index, attempt]() { queryClientStatus(index, attempt + 1); });
      return;
      }
    }
    if (status.totalBytes <= 0 && policy.configuredCapacityBytes > 0)
      status.totalBytes = policy.configuredCapacityBytes;
    if (status.freeBytes < 0 && policy.configuredCapacityBytes > 0)
      status.freeBytes = qMax<qint64>(0, policy.configuredCapacityBytes -
                                          conservativeAllocatedBytes(policy.clientId, status.torrents));
    const bool clientReady = m_config.dryRun
                               ? !circuitBreakerOpen(m_clients.at(index).id)
                               : updateCircuitBreaker(m_clients.at(index).id, status.reachable, status.detail);
    if (status.reachable && !clientReady) {
      status.reachable = false;
      status.detail = tr("Recovery check passed; waiting for another consecutive successful check before routing.");
    }
    m_statuses[index] = status;
    client->deleteLater();
    if (--m_pendingStatusQueries == 0) {
      if (!m_config.dryRun) reconcileManagedState();
      processNextJob();
    }
  });
  client->fetchStatus();
}

QList<int> TorrentAutomationEngine::eligibleClientIndexes(const Job& job) const {
  QList<int> result;
  for (int i = 0; i < m_clients.size(); ++i) {
    if (!clientRestriction(i, job).isEmpty()) continue;
    result.append(i);
  }
  return result;
}

qint64 TorrentAutomationEngine::clientFreeSpaceTarget(int index) const {
  const TorrentClientStatus& status = m_statuses.at(index);
  const TorrentAutomationClientPolicy policy = m_config.policyFor(m_clients.at(index).id);
  qint64 target = policy.minimumFreeBytes;
  const qint64 capacity = status.totalBytes > 0 ? status.totalBytes : policy.configuredCapacityBytes;
  if (capacity > 0 && policy.targetFreePercent > 0.0)
    target = qMax(target, qint64(double(capacity) * policy.targetFreePercent / 100.0));
  if (m_config.cleanupEnabled && policy.allowCleanup && m_config.cleanupStopFreeEnabled)
    target = qMax(target, m_config.cleanupStopFreeBytes);
  // Live free-space values describe bytes used right now, so reserve unfinished
  // managed bytes separately. Capacity fallbacks already reserve the full size
  // of every listed torrent and every pending ledger entry.
  if (m_config.reserveRemainingBytes && status.liveSpace)
    target += outstandingManagedBytes(m_clients.at(index).id);
  return target;
}

QString TorrentAutomationEngine::clientRestriction(int index,
                                                    const Job& job,
                                                    bool* softRestriction) const {
  if (softRestriction != nullptr) *softRestriction = false;
  const TorrentClientConfig& client = m_clients.at(index);
  const TorrentClientStatus& status = m_statuses.at(index);
  const TorrentAutomationClientPolicy policy = m_config.policyFor(client.id);
  if (!job.allowedClientIds.isEmpty() && !job.allowedClientIds.contains(client.id))
    return tr("Not allowed by the matching RSS rule");
  if (job.attemptedClientIds.contains(client.id)) return tr("This destination already failed during this attempt");
  if (!status.reachable) return status.detail.isEmpty() ? tr("Client is unavailable") : status.detail;
  if (status.freeBytes >= 0 && status.freeBytes < job.sizeBytes)
    return tr("Only %1 GB is free; the torrent needs approximately %2 GB")
      .arg(status.freeBytes / 1000000000.0, 0, 'f', 1).arg(job.sizeBytes / 1000000000.0, 0, 'f', 1);
  if (job.directOverride) return {};
  if (policy.maxActiveDownloads > 0 && status.activeDownloads >= policy.maxActiveDownloads) {
    if (softRestriction != nullptr) *softRestriction = true;
    return tr("%1 active downloads; configured maximum is %2")
      .arg(status.activeDownloads).arg(policy.maxActiveDownloads);
  }
  if (policy.maximumDownloadBytesPerSecond > 0 && status.downloadBytesPerSecond >= policy.maximumDownloadBytesPerSecond) {
    if (softRestriction != nullptr) *softRestriction = true;
    return tr("Current download rate %1 MiB/s exceeds the %2 MiB/s limit")
      .arg(status.downloadBytesPerSecond / (1024.0 * 1024.0), 0, 'f', 1)
      .arg(policy.maximumDownloadBytesPerSecond / (1024.0 * 1024.0), 0, 'f', 1);
  }
  if (policy.maxManagedTorrents > 0) {
    int managed = 0;
    for (const TorrentRemoteItem& item : status.torrents) managed += item.managedByAutomation ? 1 : 0;
    if (managed >= policy.maxManagedTorrents) {
      if (softRestriction != nullptr) *softRestriction = true;
      return tr("%1 managed torrents; configured maximum is %2").arg(managed).arg(policy.maxManagedTorrents);
    }
  }
  if (status.freeBytes >= 0 && status.freeBytes - job.sizeBytes < clientFreeSpaceTarget(index)) {
    if (softRestriction != nullptr) *softRestriction = true;
    return tr("Sending would reduce free space below the configured safety target");
  }
  return {};
}

int TorrentAutomationEngine::selectClient(const QList<int>& eligible) {
  if (eligible.isEmpty()) return -1;
  if (m_config.strategy == TorrentRoutingStrategy::Priority) {
    return *std::min_element(eligible.cbegin(), eligible.cend(), [this](int left, int right) {
      return m_config.policyFor(m_clients.at(left).id).priority <
             m_config.policyFor(m_clients.at(right).id).priority;
    });
  }
  if (m_config.strategy == TorrentRoutingStrategy::RoundRobin) {
    const int selected = eligible.at(m_config.roundRobinCursor % eligible.size());
    ++m_config.roundRobinCursor;
    if (!m_config.dryRun) m_config.save(qApp->settings());
    return selected;
  }
  if (m_config.strategy == TorrentRoutingStrategy::Weighted) {
    int highestPriority = 1;
    for (int index : eligible) highestPriority = qMax(highestPriority, m_config.policyFor(m_clients.at(index).id).priority);
    int total = 0;
    for (int index : eligible)
      total += qMax(1, highestPriority + 1 - m_config.policyFor(m_clients.at(index).id).priority);
    int point = m_config.roundRobinCursor++ % total;
    if (!m_config.dryRun) m_config.save(qApp->settings());
    for (int index : eligible) {
      point -= qMax(1, highestPriority + 1 - m_config.policyFor(m_clients.at(index).id).priority);
      if (point < 0) return index;
    }
  }

  int best = eligible.first();
  double bestScore = -std::numeric_limits<double>::max();
  for (int index : eligible) {
    const TorrentClientStatus& status = m_statuses.at(index);
    const TorrentAutomationClientPolicy policy = m_config.policyFor(m_clients.at(index).id);
    double score = 0.0;
    if (m_config.strategy == TorrentRoutingStrategy::LeastBusy) score = -status.activeDownloads;
    else if (m_config.strategy == TorrentRoutingStrategy::MostFreeSpace) score = status.freeBytes;
    else {
      const double freeRatio = status.totalBytes > 0 ? double(status.freeBytes) / double(status.totalBytes) : 0.25;
      const double downloadMiB = qMax<qint64>(0, status.downloadBytesPerSecond) / (1024.0 * 1024.0);
      score = freeRatio * 1000.0 - status.activeDownloads * 200.0 - status.queuedDownloads * 80.0 -
              downloadMiB * 3.0 - policy.priority * 25.0;
      if (m_clients.at(index).id == m_lastSelectedClientId &&
          m_consecutiveAssignments >= m_config.maximumConsecutiveAssignments && eligible.size() > 1)
        score -= 1000.0;
    }
    if (score > bestScore) { bestScore = score; best = index; }
  }
  return best;
}

int TorrentAutomationEngine::confirmManualDestination(const Job& job,
                                                       const QList<int>& eligible,
                                                       int recommended) {
  QDialog dialog(m_manualDialogParent == nullptr ? qApp->mainFormWidget() : m_manualDialogParent.data());
  dialog.setWindowTitle(tr("Torrent routing decision"));
  dialog.setMinimumWidth(560);
  auto* layout = new QVBoxLayout(&dialog);
  auto* heading = new QLabel(tr("Choose where to process “%1”. Policy restrictions can be overridden; unavailable clients cannot.")
                               .arg(job.title.toHtmlEscaped()), &dialog);
  heading->setWordWrap(true); layout->addWidget(heading);

  auto* results = new QWidget(&dialog);
  auto* resultsLayout = new QVBoxLayout(results);
  resultsLayout->setContentsMargins(0, 4, 0, 4);
  auto* destination = new QComboBox(&dialog);
  for (int index = 0; index < m_clients.size(); ++index) {
    bool soft = false;
    const QString restriction = clientRestriction(index, job, &soft);
    const bool allowed = restriction.isEmpty();
    QString colour;
    QString state;
    if (index == recommended) { colour = QStringLiteral("#1877d2"); state = tr("Best choice"); }
    else if (allowed) { colour = QStringLiteral("#24934c"); state = tr("Suitable"); }
    else if (soft) { colour = QStringLiteral("#d58a00"); state = tr("Override available"); }
    else { colour = QStringLiteral("#c83232"); state = tr("Unavailable"); }
    auto* line = new QLabel(QStringLiteral("<b style='color:%1'>● %2 — %3</b>%4")
                              .arg(colour, m_clients.at(index).name.toHtmlEscaped(), state.toHtmlEscaped(),
                                   restriction.isEmpty() ? QString() : QStringLiteral("<br><small>%1</small>").arg(restriction.toHtmlEscaped())), results);
    line->setWordWrap(true);
    line->setStyleSheet(QStringLiteral("border-left:4px solid %1; padding:4px 8px;").arg(colour));
    resultsLayout->addWidget(line);
    if (allowed || soft) {
      destination->addItem(soft ? tr("%1 — override warning").arg(m_clients.at(index).name)
                                : m_clients.at(index).name, index);
      if (index == recommended) destination->setCurrentIndex(destination->count() - 1);
    }
  }
  layout->addWidget(results);
  auto* form = new QFormLayout();
  form->addRow(tr("Destination:"), destination);
  layout->addLayout(form);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
  auto* send = buttons->addButton(m_config.dryRun ? tr("Simulate selected destination") : tr("Send to selected destination"),
                                  QDialogButtonBox::AcceptRole);
  auto* queue = buttons->addButton(tr("Queue and retry"), QDialogButtonBox::ActionRole);
  send->setEnabled(destination->count() > 0);
  connect(send, &QPushButton::clicked, &dialog, &QDialog::accept);
  connect(queue, &QPushButton::clicked, &dialog, [&dialog]() { dialog.done(2); });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  const int result = dialog.exec();
  if (result == 2) return -2;
  if (result != QDialog::Accepted || destination->currentIndex() < 0) return -1;
  return destination->currentData().toInt();
}

void TorrentAutomationEngine::processNextJob() {
  while (!m_jobs.isEmpty() && !m_jobs.head().directOverride && !m_jobs.head().retentionCleanup &&
         wasProcessed(m_jobs.head().key)) m_jobs.dequeue();
  if (m_jobs.isEmpty()) { finishBatch(); return; }
  const Job job = m_jobs.dequeue();
  if (m_config.paused && !job.directOverride && !job.manualApproval && !m_forcedDryRun) {
    Job held = job;
    held.nextAttempt = QDateTime::currentDateTimeUtc().addSecs(60);
    held.queueReason = tr("Automation is paused");
    m_deferredJobs.append(held);
    saveRuntime();
    processNextJob();
    return;
  }
  if (job.retentionCleanup) {
    m_lastRetentionCheck = QDateTime::currentDateTimeUtc();
    if (processRetentionCleanup(job)) return;
    processNextJob();
    return;
  }
  if (!job.directOverride && m_config.scheduleEnabled &&
      !withinHourWindow(m_config.scheduleStartHour, m_config.scheduleEndHour)) {
    if (m_config.dryRun) {
      record(QStringLiteral("DRY RUN — SCHEDULE"), job, {},
             tr("Rule: %1. Automatic routing would wait because the current time is outside the configured routing hours. No item was queued and no changes were made.")
               .arg(job.ruleName));
      notify(tr("DRY RUN  •  WOULD WAIT"),
             tr("TORRENT\n%1\n\nWHY\nOutside the configured routing hours\n\nNEXT STEP\nIt would be processed in the next allowed window\n\n✓ TEST ONLY — nothing was queued or changed").arg(job.title));
      processNextJob();
      return;
    }
    scheduleForWindow(job, m_config.scheduleStartHour,
                      tr("Outside the configured automatic-routing hours."));
    processNextJob();
    return;
  }
  if (!job.directOverride && job.verificationClientId.isEmpty() && m_config.preventDuplicateAcrossClients) {
    const QString hash = magnetInfoHash(job.url);
    bool existingCopy = false;
    for (const TorrentClientStatus& status : m_statuses)
      for (const TorrentRemoteItem& item : status.torrents)
        if (!hash.isEmpty() && item.hash.compare(hash, Qt::CaseInsensitive) == 0) existingCopy = true;
    if (existingCopy) {
      if (!m_config.dryRun) markProcessed(job.key);
      record(m_config.dryRun ? QStringLiteral("DRY RUN — DUPLICATE") : QStringLiteral("already-present"), job, {},
             m_config.dryRun
               ? tr("Rule: %1. This torrent was found on a configured client, so it would be skipped as a duplicate. No processed marker was written and no changes were made.").arg(job.ruleName)
               : tr("This torrent already exists on a configured client; automatic duplicate submission was skipped."));
      if (m_config.dryRun)
        notify(tr("DRY RUN  •  WOULD SKIP"),
               tr("TORRENT\n%1\n\nWHY\nA matching torrent already exists on a configured client\n\n✓ TEST ONLY — nothing was sent or changed").arg(job.title));
      processNextJob();
      return;
    }
  }
  if (!job.verificationClientId.isEmpty()) {
    const QString infoHash = magnetInfoHash(job.url);
    int verificationIndex = -1;
    for (int index = 0; index < m_clients.size(); ++index)
      if (m_clients.at(index).id == job.verificationClientId) { verificationIndex = index; break; }
    if (verificationIndex < 0 || !m_statuses.at(verificationIndex).reachable) {
      scheduleRetry(job, tr("Waiting to verify whether the previous timed-out submission was accepted."));
      processNextJob();
      return;
    }
    bool found = false;
    for (const TorrentRemoteItem& item : m_statuses.at(verificationIndex).torrents)
      if (!infoHash.isEmpty() && item.hash.compare(infoHash, Qt::CaseInsensitive) == 0) { found = true; break; }
    if (found) {
      markProcessed(job.key);
      TorrentSendHistory::instance(qApp)->markSent({job.messageId}, job.verificationClientId);
      record(QStringLiteral("verified"), job, job.verificationClientId,
             tr("The earlier timed-out submission was found on the client; no duplicate was sent."));
      saveRuntime();
      processNextJob();
      return;
    }
    Job verified = job;
    verified.attemptedClientIds.append(job.verificationClientId);
    verified.verificationClientId.clear();
    m_jobs.prepend(verified);
    processNextJob();
    return;
  }
  const QList<int> eligible = eligibleClientIndexes(job);
  int selected = selectClient(eligible);
  if (m_config.dryRun && !job.directOverride) recordDryRunClientAssessments(job, selected);
  if (selected < 0) {
    if (tryCleanup(job)) return;
    if (m_config.dryRun && !job.directOverride) {
      const QString retry = m_config.retryEnabled
                              ? tr("Live automation would place the item in the retry queue using the configured retry limits and delay.")
                              : tr("Retries are disabled, so live automation would record a final failure.");
      record(QStringLiteral("DRY RUN — FINAL"), job, {},
             tr("No client is currently suitable and safe cleanup could not make enough space. %1 No item was queued, sent or removed.")
               .arg(retry));
      notify(tr("DRY RUN  •  BLOCKED"),
             tr("TORRENT\n%1\n\nOUTCOME\nNo client is currently suitable\n\nWHY\nSafe cleanup could not make enough space\n\nLIVE BEHAVIOUR\n%2\n\n✓ TEST ONLY — nothing was queued, sent or removed")
               .arg(job.title, retry), true);
      processNextJob();
      return;
    }
    if (job.manualApproval) {
      const int decision = confirmManualDestination(job, eligible, selected);
      if (decision >= 0) { sendJob(job, decision); return; }
      if (decision == -2) scheduleRetry(job, tr("Manual processing was queued until a client becomes suitable."));
      else record(QStringLiteral("cancelled"), job, {}, tr("Manual processing was cancelled."));
      processNextJob();
      return;
    }
    record(QStringLiteral("held"), job, {}, tr("No healthy client is within its configured limits."));
    notify(tr("Torrent held for retry"), tr("%1 — no client currently has safe capacity.").arg(job.title), true);
    scheduleRetry(job, tr("No healthy client is currently within its configured limits."));
    processNextJob();
    return;
  }
  if (job.manualApproval) {
    const int decision = confirmManualDestination(job, eligible, selected);
    if (decision == -2) {
      scheduleRetry(job, tr("Manual processing was queued for retry."));
      processNextJob();
      return;
    }
    if (decision < 0) {
      record(QStringLiteral("cancelled"), job, {}, tr("Manual processing was cancelled."));
      processNextJob();
      return;
    }
    selected = decision;
  }
  const QString selectedId = m_clients.at(selected).id;
  if (selectedId == m_lastSelectedClientId) ++m_consecutiveAssignments;
  else { m_lastSelectedClientId = selectedId; m_consecutiveAssignments = 1; }
  sendJob(job, selected);
}

void TorrentAutomationEngine::recordDryRunClientAssessments(const Job& job, int selected) {
  record(QStringLiteral("DRY RUN — ITEM"), job, {},
         tr("Testing “%1” against rule “%2”; estimated torrent size %3 GB. Routing strategy: %4. Live client status was read, but no client or torrent was changed.")
           .arg(job.title, job.ruleName)
           .arg(job.sizeBytes / 1000000000.0, 0, 'f', 2)
           .arg(TorrentAutomationConfig::strategyName(m_config.strategy)));

  for (int index = 0; index < m_clients.size(); ++index) {
    bool soft = false;
    const QString restriction = clientRestriction(index, job, &soft);
    const TorrentClientStatus& status = m_statuses.at(index);
    const TorrentAutomationClientPolicy policy = m_config.policyFor(m_clients.at(index).id);
    const QString level = index == selected ? tr("BLUE — selected")
                          : restriction.isEmpty() ? tr("GREEN — suitable")
                          : soft ? tr("AMBER — policy restricted")
                                 : tr("RED — unavailable or unsafe");
    const QString space = status.freeBytes < 0
                            ? tr("free space unknown")
                            : tr("%1 GB free of %2 GB; safety target %3 GB")
                                .arg(status.freeBytes / 1000000000.0, 0, 'f', 1)
                                .arg(status.totalBytes > 0 ? status.totalBytes / 1000000000.0 : 0.0, 0, 'f', 1)
                                .arg(clientFreeSpaceTarget(index) / 1000000000.0, 0, 'f', 1);
    const QString reason = restriction.isEmpty()
                             ? (index == selected ? tr("highest result under the configured routing strategy")
                                                  : tr("eligible, but ranked below the selected client"))
                             : restriction;
    record(QStringLiteral("DRY RUN — CLIENT"), job, m_clients.at(index).id,
           tr("%1: %2 — %3. Priority %4; %5 active and %6 queued downloads; downloading %7 MiB/s; uploading %8 MiB/s; %9.")
             .arg(m_clients.at(index).name, level, reason)
             .arg(policy.priority)
             .arg(status.activeDownloads)
             .arg(status.queuedDownloads)
             .arg(qMax<qint64>(0, status.downloadBytesPerSecond) / (1024.0 * 1024.0), 0, 'f', 1)
             .arg(qMax<qint64>(0, status.uploadBytesPerSecond) / (1024.0 * 1024.0), 0, 'f', 1)
             .arg(space));
  }
}

QString TorrentAutomationEngine::cleanupEligibilityDetail(const TorrentRemoteItem& item,
                                                           const QDateTime& now) const {
  QStringList reasons;
  reasons.append(m_config.smartCleanupOrder ? tr("selected by Smart combined score")
                                            : tr("selected by oldest-completed-first order"));
  if (m_config.minimumSeedHoursEnabled)
    reasons.append(tr("seeded/completed for %1 h (minimum %2 h)")
                     .arg(item.completed.isValid() ? item.completed.secsTo(now) / 3600 : 0)
                     .arg(m_config.minimumSeedHours));
  if (m_config.minimumRatioEnabled)
    reasons.append(tr("ratio %1 (minimum %2)").arg(item.ratio, 0, 'f', 2).arg(m_config.minimumRatio, 0, 'f', 2));
  if (m_config.minimumInactiveHoursEnabled)
    reasons.append(tr("inactive for %1 h (minimum %2 h)")
                     .arg(item.lastActivity.isValid() ? item.lastActivity.secsTo(now) / 3600 : 0)
                     .arg(m_config.minimumInactiveHours));
  if (m_config.protectUploadingEnabled)
    reasons.append(tr("upload %1 KiB/s is below protection threshold %2 KiB/s")
                     .arg(qMax<qint64>(0, item.uploadBytesPerSecond) / 1024.0, 0, 'f', 1)
                     .arg(m_config.protectUploadBytesPerSecond / 1024.0, 0, 'f', 1));
  if (!m_config.protectedTags.isEmpty()) reasons.append(tr("does not have a protected tag"));
  if (!m_config.protectedTrackerTerms.isEmpty()) reasons.append(tr("tracker is not protected"));
  if (m_config.minimumCopiesEnabled)
    reasons.append(tr("copy-protection check passed (%1 completed copies)").arg(completedCopyCount(item.hash)));
  return reasons.join(QStringLiteral("; "));
}

bool TorrentAutomationEngine::simulateCleanup(const Job& job) {
  const int removalLimit = m_config.maximumRemovalsEnabled ? m_config.maximumRemovalsPerRun : 25;
  if (!m_config.deleteData) {
    record(QStringLiteral("DRY RUN — CLEANUP BLOCKED"), job, {},
           tr("Space-pressure cleanup would not remove anything because keeping downloaded data cannot recover disk space."));
    return false;
  }
  const QDateTime now = QDateTime::currentDateTimeUtc();
  bool inspectedCleanupClient = false;

  if (m_config.cleanupScheduleEnabled &&
      !withinHourWindow(m_config.cleanupScheduleStartHour, m_config.cleanupScheduleEndHour)) {
    record(QStringLiteral("DRY RUN — CLEANUP"), job, {},
           tr("Cleanup would wait for the configured maintenance window. No retry was scheduled and no changes were made."));
    return true;
  }

  for (int i = 0; i < m_clients.size(); ++i) {
    const TorrentAutomationClientPolicy policy = m_config.policyFor(m_clients.at(i).id);
    const TorrentClientStatus& status = m_statuses.at(i);
    const qint64 originalFree = status.freeBytes;
    if (!policy.allowCleanup || !status.reachable || status.freeBytes < 0 ||
        job.attemptedClientIds.contains(m_clients.at(i).id)) continue;

    qint64 target = clientFreeSpaceTarget(i) + job.sizeBytes;
    const qint64 capacity = status.totalBytes > 0 ? status.totalBytes : policy.configuredCapacityBytes;
    if (capacity > 0 && m_config.cleanupBatchPercent > 0.0) {
      const qint64 batch = qMax<qint64>(1, qint64(double(capacity) * m_config.cleanupBatchPercent / 100.0));
      target = qint64(std::ceil(double(target) / double(batch))) * batch;
    }
    if (status.freeBytes >= target) continue;
    inspectedCleanupClient = true;
    record(QStringLiteral("DRY RUN — CLEANUP TARGET"), job, m_clients.at(i).id,
           tr("%1 has %2 GB free. It would need %3 GB free after allowing for this %4 GB torrent, configured reserves and cleanup batching; approximately %5 GB must be recovered.")
             .arg(m_clients.at(i).name)
             .arg(status.freeBytes / 1000000000.0, 0, 'f', 1)
             .arg(target / 1000000000.0, 0, 'f', 1)
             .arg(job.sizeBytes / 1000000000.0, 0, 'f', 1)
             .arg((target - status.freeBytes) / 1000000000.0, 0, 'f', 1));

    QList<TorrentRemoteItem> eligible;
    for (const TorrentRemoteItem& item : status.torrents) {
      if (!item.managedByAutomation || item.hash.isEmpty()) continue;
      QString reason;
      if (item.progress < 1.0 || item.downloading) reason = tr("still downloading or incomplete");
      bool protectedByName = false;
      for (const QString& tag : item.tags)
        if (m_config.protectedTags.contains(tag, Qt::CaseInsensitive)) { protectedByName = true; break; }
      if (!protectedByName)
        for (const QString& tracker : m_config.protectedTrackerTerms)
          if (!tracker.trimmed().isEmpty() && item.tracker.contains(tracker.trimmed(), Qt::CaseInsensitive)) {
            protectedByName = true; break;
          }
      if (reason.isEmpty() && protectedByName) reason = tr("protected tag or tracker matched");
      if (reason.isEmpty() && m_config.minimumCopiesEnabled &&
          completedCopyCount(item.hash) <= m_config.minimumCopiesAcrossClients)
        reason = tr("minimum-copy protection would be breached");
      if (reason.isEmpty() && m_config.protectUploadingEnabled && item.uploadBytesPerSecond < 0 &&
          m_config.protectWhenSpeedUnknown) reason = tr("upload speed is unknown and unknown speeds are protected");
      if (reason.isEmpty() && m_config.protectUploadingEnabled &&
          item.uploadBytesPerSecond >= m_config.protectUploadBytesPerSecond)
        reason = tr("uploading at %1 KiB/s, at or above the %2 KiB/s protection threshold")
                   .arg(item.uploadBytesPerSecond / 1024.0, 0, 'f', 1)
                   .arg(m_config.protectUploadBytesPerSecond / 1024.0, 0, 'f', 1);
      const QDateTime recentUpload = lastUploadActivity(m_clients.at(i).id, item.hash);
      if (reason.isEmpty() && m_config.protectRecentUploadHours > 0 && recentUpload.isValid() &&
          recentUpload.secsTo(now) < qint64(m_config.protectRecentUploadHours) * 3600)
        reason = tr("uploaded recently and is inside the %1-hour protection period").arg(m_config.protectRecentUploadHours);
      if (reason.isEmpty() && m_config.minimumRatioEnabled && item.ratio < m_config.minimumRatio)
        reason = tr("ratio %1 is below the required %2").arg(item.ratio, 0, 'f', 2).arg(m_config.minimumRatio, 0, 'f', 2);
      if (reason.isEmpty() && m_config.minimumSeedHoursEnabled &&
          (!item.completed.isValid() || item.completed.secsTo(now) < qint64(m_config.minimumSeedHours) * 3600))
        reason = tr("minimum seeding/completed age has not been reached");
      if (reason.isEmpty() && m_config.minimumInactiveHoursEnabled &&
          (!item.lastActivity.isValid() || item.lastActivity.secsTo(now) < qint64(m_config.minimumInactiveHours) * 3600))
        reason = tr("minimum inactivity period has not been reached");
      if (!reason.isEmpty()) {
        record(QStringLiteral("DRY RUN — PROTECTED"), job, m_clients.at(i).id,
               tr("Would keep “%1” (%2 GB): %3.")
                 .arg(item.name).arg(item.sizeBytes / 1000000000.0, 0, 'f', 1).arg(reason));
      }
      else if (m_config.cleanupGraceEnabled) {
        QDateTime markedAt;
        for (const QJsonValue& value : std::as_const(m_cleanupCandidates)) {
          const QJsonObject candidate = value.toObject();
          if (candidate.value(QStringLiteral("clientId")).toString() == m_clients.at(i).id &&
              candidate.value(QStringLiteral("hash")).toString().compare(item.hash, Qt::CaseInsensitive) == 0) {
            markedAt = QDateTime::fromString(candidate.value(QStringLiteral("markedAt")).toString(), Qt::ISODate);
            break;
          }
        }
        if (!markedAt.isValid()) {
          record(QStringLiteral("DRY RUN — WOULD MARK"), job, m_clients.at(i).id,
                 tr("Would mark “%1” as a cleanup candidate because %2. Live automation would wait %3 hour(s), then completely recheck it before removal. The dry run did not create the mark.")
                   .arg(item.name, cleanupEligibilityDetail(item, now)).arg(m_config.cleanupGraceHours));
        }
        else {
          const QDateTime ready = markedAt.addSecs(qMax(1, m_config.cleanupGraceHours) * 3600);
          if (ready > now)
            record(QStringLiteral("DRY RUN — GRACE WAIT"), job, m_clients.at(i).id,
                   tr("Would keep “%1” for now because its cleanup grace period ends at %2. It would be fully rechecked then.")
                     .arg(item.name, ready.toLocalTime().toString(QStringLiteral("dd/MM/yyyy HH:mm:ss"))));
          else eligible.append(item);
        }
      }
      else eligible.append(item);
    }

    std::sort(eligible.begin(), eligible.end(), [this, now](const TorrentRemoteItem& a, const TorrentRemoteItem& b) {
      if (m_config.smartCleanupOrder) return cleanupScore(a, now) > cleanupScore(b, now);
      const QDateTime aDate = a.completed.isValid() ? a.completed : a.added;
      const QDateTime bDate = b.completed.isValid() ? b.completed : b.added;
      return aDate < bDate;
    });

    qint64 simulatedFree = status.freeBytes;
    int proposed = 0;
    const int availableRemovals = qMax(0, removalLimit - m_cleanupCount);
    QStringList proposedHashes;
    for (const TorrentRemoteItem& item : std::as_const(eligible)) {
      if (simulatedFree >= target || proposed >= availableRemovals) break;
      ++proposed;
      proposedHashes.append(item.hash);
      simulatedFree += qMax<qint64>(0, item.sizeBytes);
      record(QStringLiteral("DRY RUN — WOULD REMOVE"), job, m_clients.at(i).id,
             tr("Would remove “%1” from %2%3; size %4 GB; %5. Estimated free space afterwards: %6 GB. No changes made.")
               .arg(item.name, m_clients.at(i).name,
                    m_config.deleteData ? tr(" and delete its downloaded data") : tr(" but keep its downloaded data"))
               .arg(item.sizeBytes / 1000000000.0, 0, 'f', 1)
               .arg(cleanupEligibilityDetail(item, now))
               .arg(simulatedFree / 1000000000.0, 0, 'f', 1));
    }

    if (proposed > 0) {
      m_cleanupCount += proposed;
      m_statuses[i].freeBytes = simulatedFree;
      for (int torrentIndex = m_statuses[i].torrents.size() - 1; torrentIndex >= 0; --torrentIndex)
        if (proposedHashes.contains(m_statuses[i].torrents.at(torrentIndex).hash, Qt::CaseInsensitive))
          m_statuses[i].torrents.removeAt(torrentIndex);
    }

    if (simulatedFree >= target) {
      record(QStringLiteral("DRY RUN — FINAL"), job, m_clients.at(i).id,
             tr("Cleanup would remove %1 torrent(s), recover approximately %2 GB, reach the configured target and then send “%3” to %4. No torrent was removed, no data was deleted and nothing was sent.")
               .arg(proposed)
               .arg((simulatedFree - originalFree) / 1000000000.0, 0, 'f', 1)
               .arg(job.title, m_clients.at(i).name));
      notify(tr("DRY RUN  •  WOULD CLEAN UP AND SEND"),
             tr("TORRENT\n%1\n\nDESTINATION\n%2\n\nCLEANUP\n%3 torrent(s) would be removed\nApproximately %4 GB would be recovered\n\n✓ TEST ONLY — no torrent or data was removed and nothing was sent")
               .arg(job.title, m_clients.at(i).name)
               .arg(proposed)
               .arg((simulatedFree - originalFree) / 1000000000.0, 0, 'f', 1));
      ++m_statuses[i].activeDownloads;
      if (!m_config.reserveRemainingBytes || !m_statuses[i].liveSpace)
        m_statuses[i].freeBytes = qMax<qint64>(0, m_statuses[i].freeBytes - job.sizeBytes);
      processNextJob();
      return true;
    }
    else {
      const QString limitReason = proposed >= availableRemovals
                                    ? tr("the configured maximum-removals limit was reached")
                                    : tr("no more eligible torrents remained");
      record(QStringLiteral("DRY RUN — CLEANUP INSUFFICIENT"), job, m_clients.at(i).id,
             tr("Cleanup would recover approximately %1 GB, but would still not reach the %2 GB target because %3. No changes made.")
               .arg((simulatedFree - originalFree) / 1000000000.0, 0, 'f', 1)
               .arg(target / 1000000000.0, 0, 'f', 1)
               .arg(limitReason));
      notify(tr("DRY RUN  •  CLEANUP NOT ENOUGH"),
             tr("TORRENT\n%1\n\nCLIENT\n%2\n\nOUTCOME\nAbout %3 GB could be recovered, but the %4 GB target would not be reached\n\nWHY\n%5\n\n✓ TEST ONLY — nothing was removed or changed")
               .arg(job.title, m_clients.at(i).name)
               .arg((simulatedFree - originalFree) / 1000000000.0, 0, 'f', 1)
               .arg(target / 1000000000.0, 0, 'f', 1)
               .arg(limitReason), true);
    }
  }
  if (inspectedCleanupClient) {
    processNextJob();
    return true;
  }
  return false;
}

void TorrentAutomationEngine::sendJob(const Job& job, int clientIndex) {
  TorrentClientConfig config = m_clients.at(clientIndex);
  if (!config.tags.contains(AutomationTag)) config.tags.append(AutomationTag);
  const TorrentClientStatus status = m_statuses.at(clientIndex);
  const TorrentAutomationClientPolicy policy = m_config.policyFor(config.id);
  const QString decision = tr("priority %1, %2 active download(s), %3 MiB/s downloading, %4 free")
                             .arg(policy.priority)
                             .arg(status.activeDownloads)
                             .arg(qMax<qint64>(0, status.downloadBytesPerSecond) / (1024.0 * 1024.0), 0, 'f', 1)
                             .arg(status.freeBytes < 0 ? tr("space unknown")
                                                       : QStringLiteral("%1 GB").arg(status.freeBytes / 1000000000.0, 0, 'f', 1));
  if (m_config.dryRun && !job.directOverride) {
    record(QStringLiteral("DRY RUN"), job, config.id,
           tr("Rule: %1. Would send to %2 (%3).").arg(job.ruleName, config.name, decision));
    notify(tr("DRY RUN  •  WOULD SEND"),
           tr("TORRENT\n%1\n\nDESTINATION\n%2\n\nMATCHED RULE\n%3\n\nWHY THIS CLIENT\n%4\n\n✓ TEST ONLY — nothing was sent or changed")
             .arg(job.title, config.name, job.ruleName, decision));
    ++m_statuses[clientIndex].activeDownloads;
    if (m_statuses[clientIndex].freeBytes >= 0 &&
        (!m_config.reserveRemainingBytes || !m_statuses[clientIndex].liveSpace))
      m_statuses[clientIndex].freeBytes = qMax<qint64>(0, m_statuses[clientIndex].freeBytes - job.sizeBytes);
    processNextJob();
    return;
  }

  TorrentClient* client = TorrentClient::create(config, this);
  connect(client, &TorrentClient::addFinished, this, [this, client, job, config, clientIndex, decision](int added, int failed, const QString& message) {
    if (added > 0 && failed == 0) {
      markProcessed(job.key);
      TorrentSendHistory::instance(qApp)->markSent({job.messageId}, config.id);
      m_managed.append(QJsonObject{{QStringLiteral("key"), job.key},
                                   {QStringLiteral("clientId"), config.id},
                                   {QStringLiteral("infoHash"), magnetInfoHash(job.url)},
                                   {QStringLiteral("sizeBytes"), job.sizeBytes},
                                   {QStringLiteral("remainingBytes"), job.sizeBytes},
                                   {QStringLiteral("added"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
      record(QStringLiteral("sent"), job, config.id, message);
      const bool showDirectSuccess = qApp->settings()->value(QStringLiteral("TorrentClients"),
                                                             QStringLiteral("showSuccessNotifications"), true).toBool();
      if (!job.directOverride || showDirectSuccess)
        notify(job.directOverride ? tr("Torrent sent") : tr("Torrent sent automatically"),
               tr("“%1” was sent to %2 — %3.").arg(job.title, config.name, decision));
      ++m_statuses[clientIndex].activeDownloads;
      if (m_statuses[clientIndex].freeBytes >= 0 &&
          (!m_config.reserveRemainingBytes || !m_statuses[clientIndex].liveSpace))
        m_statuses[clientIndex].freeBytes -= job.sizeBytes;
    }
    else {
      if (isTransientFailure(message)) {
        Job failover = job;
        if (isAmbiguousFailure(message)) {
          if (magnetInfoHash(job.url).isEmpty()) {
            record(QStringLiteral("uncertain"), job, config.id,
                   tr("The request timed out and may have succeeded. This direct torrent URL cannot be safely resent automatically without risking a duplicate."));
            notify(tr("Torrent submission needs checking"), tr("%1 may already have been accepted by %2.").arg(job.title, config.name), true);
            client->deleteLater();
            saveRuntime();
            processNextJob();
            return;
          }
          failover.verificationClientId = config.id;
          scheduleRetry(failover, tr("Submission outcome is uncertain; acceptance will be checked before any failover."));
          client->deleteLater();
          saveRuntime();
          processNextJob();
          return;
        }
        failover.attemptedClientIds.append(config.id);
        m_statuses[clientIndex].reachable = false;
        m_statuses[clientIndex].detail = message;
        record(QStringLiteral("failover"), job, config.id,
               tr("%1 was temporarily unavailable; checking the next suitable client. %2").arg(config.name, message));
        m_jobs.prepend(failover);
      }
      else {
        record(QStringLiteral("failed"), job, config.id, message);
        notify(tr("Torrent automation failed"), tr("%1: %2").arg(job.title, message), true);
      }
    }
    client->deleteLater();
    saveRuntime();
    processNextJob();
  });
  client->addTorrents({job.url});
}

bool TorrentAutomationEngine::isTransientFailure(const QString& message) const {
  const QString lower = message.toLower();
  if (lower.contains(QStringLiteral("authentication")) || lower.contains(QStringLiteral("unauthorized")) ||
      lower.contains(QStringLiteral("forbidden")) || lower.contains(QStringLiteral("invalid url")) ||
      lower.contains(QStringLiteral("not supported"))) return false;
  const QStringList transient{QStringLiteral("timeout"), QStringLiteral("timed out"),
                              QStringLiteral("temporarily"), QStringLiteral("unavailable"),
                              QStringLiteral("connection refused"), QStringLiteral("connection reset"),
                              QStringLiteral("host not found"), QStringLiteral("http 429"),
                              QStringLiteral("http 502"), QStringLiteral("http 503"), QStringLiteral("http 504")};
  for (const QString& marker : transient) if (lower.contains(marker)) return true;
  return false;
}

bool TorrentAutomationEngine::isAmbiguousFailure(const QString& message) const {
  const QString lower = message.toLower();
  return lower.contains(QStringLiteral("timeout")) || lower.contains(QStringLiteral("timed out")) ||
         lower.contains(QStringLiteral("connection reset"));
}

void TorrentAutomationEngine::scheduleRetry(Job job, const QString& reason) {
  if (m_config.dryRun && !job.directOverride) {
    qint64 delay = qMax(1, m_config.retryInitialSeconds);
    if (m_config.retryExponentialBackoff) delay *= (1LL << qMin(job.attempt, 16));
    delay = qMin<qint64>(delay, qMax(1, m_config.retryMaximumSeconds));
    record(QStringLiteral("DRY RUN — RETRY"), job, {},
           m_config.retryEnabled
             ? tr("Live automation would queue retry %1 of %2 after %3 seconds: %4. The dry run did not create a queue entry.")
                 .arg(job.attempt + 1).arg(m_config.retryAttempts).arg(delay).arg(reason)
             : tr("Live automation would fail without retry because retries are disabled: %1. The dry run changed nothing.").arg(reason));
    return;
  }
  if (!m_config.retryEnabled) {
    record(QStringLiteral("failed"), job, {}, tr("Retries are disabled. %1").arg(reason));
    return;
  }
  int maximum = m_config.retryAttempts;
  QString policyClientId;
  if (!job.attemptedClientIds.isEmpty()) policyClientId = job.attemptedClientIds.constLast();
  else if (job.allowedClientIds.size() == 1) policyClientId = job.allowedClientIds.constFirst();
  if (!policyClientId.isEmpty()) {
    const int overrideAttempts = m_config.policyFor(policyClientId).retryAttempts;
    if (overrideAttempts >= 0) maximum = overrideAttempts;
  }
  if (job.attempt >= maximum) {
    record(QStringLiteral("failed"), job, {}, tr("Retry limit reached after %1 attempt(s). %2").arg(job.attempt).arg(reason));
    notify(tr("Torrent automation retry limit reached"), tr("%1 — %2").arg(job.title, reason), true);
    return;
  }
  ++job.attempt;
  job.attemptedClientIds.clear();
  job.queueReason = reason;
  qint64 delay = qMax(1, m_config.retryInitialSeconds);
  if (m_config.retryExponentialBackoff) delay *= (1LL << qMin(job.attempt - 1, 16));
  delay = qMin<qint64>(delay, qMax(1, m_config.retryMaximumSeconds));
  job.nextAttempt = QDateTime::currentDateTimeUtc().addSecs(delay);
  m_deferredJobs.append(job);
  record(QStringLiteral("queued"), job, {}, tr("Retry %1 of %2 scheduled in %3 seconds. %4")
           .arg(job.attempt).arg(maximum).arg(delay).arg(reason));
  saveRuntime();
  armDeferredJob(job);
}

void TorrentAutomationEngine::armDeferredJob(const Job& job) {
  const qint64 delayMs = qMax<qint64>(1000, QDateTime::currentDateTimeUtc().msecsTo(job.nextAttempt));
  QTimer::singleShot(int(qMin<qint64>(delayMs, std::numeric_limits<int>::max())), this,
                     [this, key = job.key, attempt = job.attempt]() {
    for (int index = 0; index < m_deferredJobs.size(); ++index) {
      if (m_deferredJobs.at(index).key != key || m_deferredJobs.at(index).attempt != attempt) continue;
      Job due = m_deferredJobs.takeAt(index);
      due.nextAttempt = {};
      m_jobs.enqueue(due);
      saveRuntime();
      if (!m_busy) beginBatch();
      return;
    }
  });
}

bool TorrentAutomationEngine::processRetentionCleanup(const Job& job) {
  if (!m_config.cleanupEnabled || !m_config.maximumRetentionEnabled) return false;
  if (m_config.cleanupScheduleEnabled &&
      !withinHourWindow(m_config.cleanupScheduleStartHour, m_config.cleanupScheduleEndHour)) {
    if (m_config.dryRun)
      record(QStringLiteral("DRY RUN — RETENTION WAIT"), job, {},
             tr("Maximum-retention cleanup is due but will wait for the configured cleanup window."));
    return false;
  }

  const QDateTime now = QDateTime::currentDateTimeUtc();
  const qint64 retentionSeconds = qint64(qMax(1, m_config.maximumRetentionHours)) * 3600;
  const int removalLimit = m_config.maximumRemovalsEnabled ? m_config.maximumRemovalsPerRun : 25;

  struct ExpiredItem {
    int clientIndex = -1;
    TorrentRemoteItem item;
    QDateTime ageBasis;
  };
  QList<ExpiredItem> expired;

  for (int clientIndex = 0; clientIndex < m_clients.size(); ++clientIndex) {
    const TorrentAutomationClientPolicy policy = m_config.policyFor(m_clients.at(clientIndex).id);
    const TorrentClientStatus& status = m_statuses.at(clientIndex);
    if (!policy.allowCleanup || !status.reachable) continue;
    if (m_config.deleteData && m_clients.at(clientIndex).type == TorrentClientType::RTorrent) {
      if (m_config.dryRun)
        record(QStringLiteral("DRY RUN — DELETE BLOCKED"), job, m_clients.at(clientIndex).id,
               tr("rTorrent XML-RPC can remove a job but cannot prove that its downloaded files were deleted. Data cleanup is blocked for this client."));
      continue;
    }

    for (const TorrentRemoteItem& item : status.torrents) {
      if (!item.managedByAutomation || item.hash.isEmpty() || item.progress < 1.0 || item.downloading) continue;
      const QDateTime ageBasis = item.completed.isValid() ? item.completed : item.added;
      if (!ageBasis.isValid() || ageBasis.secsTo(now) < retentionSeconds) continue;

      QString protection;
      for (const QString& tag : item.tags) {
        if (m_config.protectedTags.contains(tag, Qt::CaseInsensitive)) {
          protection = tr("protected tag ‘%1’ matched").arg(tag);
          break;
        }
      }
      if (protection.isEmpty()) {
        for (const QString& tracker : m_config.protectedTrackerTerms) {
          if (!tracker.trimmed().isEmpty() && item.tracker.contains(tracker.trimmed(), Qt::CaseInsensitive)) {
            protection = tr("protected tracker text ‘%1’ matched").arg(tracker.trimmed());
            break;
          }
        }
      }
      if (protection.isEmpty() && m_config.minimumCopiesEnabled &&
          completedCopyCount(item.hash) <= m_config.minimumCopiesAcrossClients)
        protection = tr("minimum-copy protection would be breached");

      if (protection.isEmpty() && !m_config.maximumRetentionStrict) {
        if (m_config.protectUploadingEnabled && item.uploadBytesPerSecond < 0 && m_config.protectWhenSpeedUnknown)
          protection = tr("upload speed is unknown and unknown speeds are protected");
        else if (m_config.protectUploadingEnabled &&
                 item.uploadBytesPerSecond >= m_config.protectUploadBytesPerSecond)
          protection = tr("the torrent is currently uploading above the protection threshold");
        const QDateTime recentUpload = lastUploadActivity(m_clients.at(clientIndex).id, item.hash);
        if (protection.isEmpty() && m_config.protectRecentUploadHours > 0 && recentUpload.isValid() &&
            recentUpload.secsTo(now) < qint64(m_config.protectRecentUploadHours) * 3600)
          protection = tr("the torrent uploaded inside the recent-activity protection period");
        if (protection.isEmpty() && m_config.minimumSeedHoursEnabled &&
            ageBasis.secsTo(now) < qint64(m_config.minimumSeedHours) * 3600)
          protection = tr("the normal minimum seeding age has not been reached");
        if (protection.isEmpty() && m_config.minimumRatioEnabled && item.ratio < m_config.minimumRatio)
          protection = tr("ratio %1 is below the normal minimum %2")
                         .arg(item.ratio, 0, 'f', 2).arg(m_config.minimumRatio, 0, 'f', 2);
        if (protection.isEmpty() && m_config.minimumInactiveHoursEnabled &&
            (!item.lastActivity.isValid() ||
             item.lastActivity.secsTo(now) < qint64(m_config.minimumInactiveHours) * 3600))
          protection = tr("the normal inactivity requirement has not been reached");
      }

      if (!protection.isEmpty()) {
        if (m_config.dryRun)
          record(QStringLiteral("DRY RUN — RETENTION PROTECTED"), job, m_clients.at(clientIndex).id,
                 tr("Would keep “%1” even though its %2-hour retention deadline has passed: %3.")
                   .arg(item.name).arg(m_config.maximumRetentionHours).arg(protection));
        continue;
      }
      expired.append(ExpiredItem{clientIndex, item, ageBasis});
    }
  }

  std::sort(expired.begin(), expired.end(), [](const ExpiredItem& left, const ExpiredItem& right) {
    return left.ageBasis < right.ageBasis;
  });

  if (m_config.dryRun) {
    const int available = qMax(0, removalLimit - m_cleanupCount);
    const int proposed = qMin(available, int(expired.size()));
    for (int index = 0; index < proposed; ++index) {
      const ExpiredItem& candidate = expired.at(index);
      record(QStringLiteral("DRY RUN — WOULD REMOVE EXPIRED"), job,
             m_clients.at(candidate.clientIndex).id,
             tr("Would remove expired torrent “%1” from %2 after %3 hours (limit %4 hours)%5. Age source: %6. This is time-based and does not depend on free-space pressure. No changes were made.")
               .arg(candidate.item.name, m_clients.at(candidate.clientIndex).name)
               .arg(candidate.ageBasis.secsTo(now) / 3600)
               .arg(m_config.maximumRetentionHours)
               .arg(m_config.deleteData ? tr(" and permanently delete its downloaded data")
                                        : tr(" while keeping its downloaded files"))
               .arg(candidate.item.ageSource.isEmpty() ? tr("client completion/added timestamp") : candidate.item.ageSource));
    }
    m_cleanupCount += proposed;
    if (expired.size() > proposed)
      record(QStringLiteral("DRY RUN — RETENTION LIMIT"), job, {},
             tr("%1 additional expired torrent(s) would wait for a later run because the maximum-removals limit was reached.")
               .arg(expired.size() - proposed));
    if (expired.isEmpty())
      record(QStringLiteral("DRY RUN — RETENTION"), job, {},
             tr("No completed RSS Guard-managed torrent has reached the configured %1-hour maximum retention deadline.")
               .arg(m_config.maximumRetentionHours));
    return false;
  }

  if (expired.isEmpty() || m_cleanupCount >= removalLimit) return false;
  const ExpiredItem candidate = expired.first();
  if (m_config.cleanupRequireConfirmation || m_config.deleteData) {
    const auto answer = QMessageBox::warning(qApp->mainFormWidget(), tr("Confirm expired torrent removal"),
      tr("“%1” on %2 has reached its %3-hour maximum retention time.%4\n\nAge measured from: %5 (%6).\n\nRemove it now? This action cannot be undone.")
        .arg(candidate.item.name, m_clients.at(candidate.clientIndex).name)
        .arg(m_config.maximumRetentionHours)
        .arg(m_config.deleteData ? tr(" Its downloaded data will also be permanently deleted.")
                                 : tr(" Its downloaded files will be kept."))
        .arg(candidate.item.ageSource.isEmpty() ? tr("client completion/added timestamp") : candidate.item.ageSource,
             candidate.ageBasis.toLocalTime().toString(QStringLiteral("dd/MM/yyyy HH:mm"))),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
      // Avoid asking about the same expired item every maintenance tick.
      m_lastRetentionCheck = now.addSecs(55 * 60);
      record(QStringLiteral("retention-declined"), job, m_clients.at(candidate.clientIndex).id,
             tr("Expired torrent removal was declined by the user; the next retention prompt is postponed for about one hour."));
      return false;
    }
  }

  TorrentClient* client = TorrentClient::create(m_clients.at(candidate.clientIndex), this);
  connect(client, &TorrentClient::removeFinished, this,
          [this, client, job, candidate, removalLimit](bool success, const QString& message) {
    if (success) {
      ++m_cleanupCount;
      if (m_config.deleteData && m_statuses[candidate.clientIndex].freeBytes >= 0)
        m_statuses[candidate.clientIndex].freeBytes += candidate.item.sizeBytes;
      for (int index = m_statuses[candidate.clientIndex].torrents.size() - 1; index >= 0; --index)
        if (m_statuses[candidate.clientIndex].torrents.at(index).hash.compare(candidate.item.hash, Qt::CaseInsensitive) == 0)
          m_statuses[candidate.clientIndex].torrents.removeAt(index);
      for (int index = m_managed.size() - 1; index >= 0; --index) {
        const QJsonObject allocation = m_managed.at(index).toObject();
        if (allocation.value(QStringLiteral("clientId")).toString() == m_clients.at(candidate.clientIndex).id &&
            allocation.value(QStringLiteral("infoHash")).toString().compare(candidate.item.hash, Qt::CaseInsensitive) == 0)
          m_managed.removeAt(index);
      }
      for (int index = m_cleanupCandidates.size() - 1; index >= 0; --index) {
        const QJsonObject marked = m_cleanupCandidates.at(index).toObject();
        if (marked.value(QStringLiteral("clientId")).toString() == m_clients.at(candidate.clientIndex).id &&
            marked.value(QStringLiteral("hash")).toString().compare(candidate.item.hash, Qt::CaseInsensitive) == 0)
          m_cleanupCandidates.removeAt(index);
      }
      record(QStringLiteral("retention-cleanup"), job, m_clients.at(candidate.clientIndex).id,
             m_config.deleteData
               ? tr("Removed expired torrent “%1” and deleted approximately %2 GB of downloaded data.")
                   .arg(candidate.item.name).arg(candidate.item.sizeBytes / 1000000000.0, 0, 'f', 1)
               : tr("Removed expired torrent “%1”; its downloaded files were kept and no disk space was claimed as recovered.")
                   .arg(candidate.item.name));
      notify(tr("Torrent retention cleanup"),
             tr("Removed expired torrent “%1” from %2%3.")
               .arg(candidate.item.name, m_clients.at(candidate.clientIndex).name,
                    m_config.deleteData ? tr(" and deleted its downloaded data") : tr(" but kept its downloaded files")));
      saveRuntime();
      if (m_cleanupCount < removalLimit) m_jobs.prepend(job);
    }
    else {
      record(QStringLiteral("retention-cleanup-failed"), job, m_clients.at(candidate.clientIndex).id,
             tr("Could not remove expired torrent “%1”: %2").arg(candidate.item.name, message));
      notify(tr("Torrent retention cleanup failed"), message, true);
    }
    client->deleteLater();
    processNextJob();
  });
  client->removeTorrent(candidate.item.hash, m_config.deleteData);
  return true;
}

bool TorrentAutomationEngine::tryCleanup(const Job& job) {
  const int removalLimit = m_config.maximumRemovalsEnabled ? m_config.maximumRemovalsPerRun : 25;
  if (!m_config.cleanupEnabled || m_cleanupCount >= removalLimit) return false;
  if (!m_config.deleteData) {
    if (m_config.dryRun)
      record(QStringLiteral("DRY RUN — CLEANUP BLOCKED"), job, {},
             tr("Space-pressure cleanup cannot recover disk space because ‘Delete downloaded data’ is off. The torrent job and its files were kept."));
    return false;
  }
  if (m_config.dryRun) return simulateCleanup(job);
  if (m_config.cleanupScheduleEnabled &&
      !withinHourWindow(m_config.cleanupScheduleStartHour, m_config.cleanupScheduleEndHour)) {
    scheduleForWindow(job, m_config.cleanupScheduleStartHour,
                      tr("Cleanup is waiting for its configured maintenance window."));
    processNextJob();
    return true;
  }
  const QDateTime now = QDateTime::currentDateTimeUtc();
  QDateTime earliestGraceReady;
  for (int i = 0; i < m_clients.size(); ++i) {
    const TorrentAutomationClientPolicy policy = m_config.policyFor(m_clients.at(i).id);
    const TorrentClientStatus& clientStatus = m_statuses.at(i);
    if (job.attemptedClientIds.contains(m_clients.at(i).id)) continue;
    qint64 cleanupTarget = clientFreeSpaceTarget(i) + job.sizeBytes;
    const qint64 capacity = clientStatus.totalBytes > 0 ? clientStatus.totalBytes : policy.configuredCapacityBytes;
    if (capacity > 0 && m_config.cleanupBatchPercent > 0.0) {
      const qint64 batch = qMax<qint64>(1, qint64(double(capacity) * m_config.cleanupBatchPercent / 100.0));
      cleanupTarget = qint64(std::ceil(double(cleanupTarget) / double(batch))) * batch;
    }
    if (!policy.allowCleanup || !clientStatus.reachable || clientStatus.freeBytes < 0 ||
        clientStatus.freeBytes >= cleanupTarget) continue;
    QList<TorrentRemoteItem> candidates;
    int protectedUploads = 0;
    const QString cleanupClientId = m_clients.at(i).id;
    const auto clearGraceCandidate = [this, &cleanupClientId](const QString& hash) {
      for (int index = m_cleanupCandidates.size() - 1; index >= 0; --index) {
        const QJsonObject candidate = m_cleanupCandidates.at(index).toObject();
        if (candidate.value(QStringLiteral("clientId")).toString() == cleanupClientId &&
            candidate.value(QStringLiteral("hash")).toString().compare(hash, Qt::CaseInsensitive) == 0)
          m_cleanupCandidates.removeAt(index);
      }
    };
    for (const TorrentRemoteItem& item : clientStatus.torrents) {
      if (!item.managedByAutomation || item.hash.isEmpty()) continue;
      if (item.progress < 1.0 || item.downloading) { clearGraceCandidate(item.hash); continue; }
      bool explicitlyProtected = false;
      for (const QString& tag : item.tags)
        if (m_config.protectedTags.contains(tag, Qt::CaseInsensitive)) { explicitlyProtected = true; break; }
      if (!explicitlyProtected) {
        for (const QString& tracker : m_config.protectedTrackerTerms)
          if (!tracker.trimmed().isEmpty() && item.tracker.contains(tracker.trimmed(), Qt::CaseInsensitive)) {
            explicitlyProtected = true;
            break;
          }
      }
      if (explicitlyProtected) { clearGraceCandidate(item.hash); continue; }
      if (m_config.minimumCopiesEnabled && completedCopyCount(item.hash) <= m_config.minimumCopiesAcrossClients) {
        clearGraceCandidate(item.hash);
        continue;
      }
      if (m_config.protectUploadingEnabled &&
          ((item.uploadBytesPerSecond < 0 && m_config.protectWhenSpeedUnknown) ||
           item.uploadBytesPerSecond >= m_config.protectUploadBytesPerSecond)) {
        ++protectedUploads;
        clearGraceCandidate(item.hash);
        continue;
      }
      const QDateTime recentUpload = lastUploadActivity(m_clients.at(i).id, item.hash);
      if (m_config.protectRecentUploadHours > 0 && recentUpload.isValid() &&
          recentUpload.secsTo(now) < qint64(m_config.protectRecentUploadHours) * 3600) {
        clearGraceCandidate(item.hash);
        continue;
      }
      if (m_config.minimumRatioEnabled && item.ratio < m_config.minimumRatio) { clearGraceCandidate(item.hash); continue; }
      if (m_config.minimumSeedHoursEnabled &&
          (!item.completed.isValid() || item.completed.secsTo(now) < qint64(m_config.minimumSeedHours) * 3600)) {
        clearGraceCandidate(item.hash); continue;
      }
      if (m_config.minimumInactiveHoursEnabled &&
          (!item.lastActivity.isValid() || item.lastActivity.secsTo(now) < qint64(m_config.minimumInactiveHours) * 3600)) {
        clearGraceCandidate(item.hash); continue;
      }
      candidates.append(item);
    }
    if (candidates.isEmpty()) {
      if (protectedUploads > 0)
        record(QStringLiteral("protected"), job, m_clients.at(i).id,
               tr("%1 old torrent(s) were retained because they are actively uploading or their speed is unavailable.")
                 .arg(protectedUploads));
      continue;
    }
    std::sort(candidates.begin(), candidates.end(), [this, now](const TorrentRemoteItem& a, const TorrentRemoteItem& b) {
      if (m_config.smartCleanupOrder) return cleanupScore(a, now) > cleanupScore(b, now);
      const QDateTime aDate = a.completed.isValid() ? a.completed : a.added;
      const QDateTime bDate = b.completed.isValid() ? b.completed : b.added;
      return aDate < bDate;
    });
    const TorrentRemoteItem victim = candidates.first();
    if (m_config.cleanupGraceEnabled) {
      int candidateIndex = -1;
      QDateTime markedAt;
      for (int index = 0; index < m_cleanupCandidates.size(); ++index) {
        const QJsonObject candidate = m_cleanupCandidates.at(index).toObject();
        if (candidate.value(QStringLiteral("clientId")).toString() == m_clients.at(i).id &&
            candidate.value(QStringLiteral("hash")).toString().compare(victim.hash, Qt::CaseInsensitive) == 0) {
          candidateIndex = index;
          markedAt = QDateTime::fromString(candidate.value(QStringLiteral("markedAt")).toString(), Qt::ISODate);
          break;
        }
      }
      if (candidateIndex < 0) {
        m_cleanupCandidates.append(QJsonObject{{QStringLiteral("clientId"), m_clients.at(i).id},
                                               {QStringLiteral("hash"), victim.hash},
                                               {QStringLiteral("name"), victim.name},
                                               {QStringLiteral("markedAt"), now.toString(Qt::ISODate)}});
        record(QStringLiteral("quarantined"), job, m_clients.at(i).id,
               tr("Marked %1 for cleanup. It will be checked again after the %2-hour grace period.")
                 .arg(victim.name).arg(m_config.cleanupGraceHours));
        saveRuntime();
        const QDateTime ready = now.addSecs(qMax(1, m_config.cleanupGraceHours) * 3600);
        if (!earliestGraceReady.isValid() || ready < earliestGraceReady) earliestGraceReady = ready;
        continue;
      }
      if (!markedAt.isValid()) markedAt = now;
      const QDateTime ready = markedAt.addSecs(qMax(1, m_config.cleanupGraceHours) * 3600);
      if (ready > now) {
        if (!earliestGraceReady.isValid() || ready < earliestGraceReady) earliestGraceReady = ready;
        continue;
      }
    }
    const bool noEligibilityFilters = !m_config.minimumSeedHoursEnabled && !m_config.minimumRatioEnabled &&
                                      !m_config.minimumInactiveHoursEnabled;
    if (m_config.cleanupRequireConfirmation || m_config.deleteData || noEligibilityFilters) {
      const auto answer = QMessageBox::warning(qApp->mainFormWidget(), tr("Confirm automatic torrent cleanup"),
        tr("Remove “%1” from %2%3 to make space for “%4”?\n\nThis action cannot be undone.")
          .arg(victim.name, m_clients.at(i).name, m_config.deleteData ? tr(" and delete its downloaded data") : QString(), job.title),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
      if (answer != QMessageBox::Yes) { clearGraceCandidate(victim.hash); continue; }
    }
    TorrentClient* client = TorrentClient::create(m_clients.at(i), this);
    connect(client, &TorrentClient::removeFinished, this, [this, client, job, i, victim](bool success, const QString& message) {
      if (success) {
        ++m_cleanupCount;
        if (m_config.deleteData && m_statuses[i].freeBytes >= 0)
          m_statuses[i].freeBytes += victim.sizeBytes;
        for (int torrentIndex = m_statuses[i].torrents.size() - 1; torrentIndex >= 0; --torrentIndex) {
          if (m_statuses[i].torrents.at(torrentIndex).hash.compare(victim.hash, Qt::CaseInsensitive) == 0) {
            m_statuses[i].torrents.removeAt(torrentIndex);
            break;
          }
        }
        for (int allocationIndex = 0; allocationIndex < m_managed.size(); ++allocationIndex) {
          const QJsonObject allocation = m_managed.at(allocationIndex).toObject();
          if (allocation.value(QStringLiteral("clientId")).toString() != m_clients.at(i).id) continue;
          const QString allocationHash = allocation.value(QStringLiteral("infoHash")).toString();
          const qint64 allocationSize = allocation.value(QStringLiteral("sizeBytes")).toVariant().toLongLong();
          if ((!allocationHash.isEmpty() && allocationHash.compare(victim.hash, Qt::CaseInsensitive) == 0) ||
              (allocationHash.isEmpty() && allocationSize == victim.sizeBytes)) {
            m_managed.removeAt(allocationIndex);
            break;
          }
        }
        for (int candidateIndex = m_cleanupCandidates.size() - 1; candidateIndex >= 0; --candidateIndex) {
          const QJsonObject candidate = m_cleanupCandidates.at(candidateIndex).toObject();
          if (candidate.value(QStringLiteral("clientId")).toString() == m_clients.at(i).id &&
              candidate.value(QStringLiteral("hash")).toString().compare(victim.hash, Qt::CaseInsensitive) == 0)
            m_cleanupCandidates.removeAt(candidateIndex);
        }
        record(QStringLiteral("cleanup"), job, m_clients.at(i).id,
               tr("Removed %1 and recovered approximately %2 GB.").arg(victim.name).arg(victim.sizeBytes / 1000000000.0, 0, 'f', 1));
        notify(tr("Torrent automation cleanup"), tr("Removed “%1” from %2 to make room for “%3”.")
                                                      .arg(victim.name, m_clients.at(i).name, job.title));
      }
      else {
        m_statuses[i].reachable = false;
        m_statuses[i].detail = message;
        record(QStringLiteral("cleanup-failed"), job, m_clients.at(i).id,
               tr("Cleanup failed on %1; this client will not be tried again during the current attempt. %2")
                 .arg(m_clients.at(i).name, message));
        notify(tr("Torrent cleanup failed"), message, true);
      }
      client->deleteLater();
      Job continuation = job;
      if (!success) continuation.attemptedClientIds.append(m_clients.at(i).id);
      m_jobs.prepend(continuation);
      processNextJob();
    });
    client->removeTorrent(victim.hash, m_config.deleteData);
    return true;
  }
  if (earliestGraceReady.isValid()) {
    scheduleAt(job, earliestGraceReady, tr("Waiting for the cleanup grace period to finish."));
    processNextJob();
    return true;
  }
  return false;
}

void TorrentAutomationEngine::finishBatch() {
  saveRuntime();
  m_busy = false;
  emit busyChanged(false);
  m_forcedDryRun = false;
  m_manualDialogParent.clear();
}

bool TorrentAutomationEngine::wasProcessed(const QString& key) const { return m_processed.contains(key); }

void TorrentAutomationEngine::markProcessed(const QString& key) {
  if (!m_processed.contains(key)) m_processed.append(key);
  while (m_processed.size() > 5000) m_processed.removeFirst();
}

qint64 TorrentAutomationEngine::conservativeAllocatedBytes(
    const QString& clientId, const QList<TorrentRemoteItem>& torrents) const {
  qint64 total = 0;
  QSet<QString> remoteHashes;
  for (const TorrentRemoteItem& torrent : torrents) {
    if (!torrent.hash.isEmpty()) remoteHashes.insert(torrent.hash.toLower());
    total += torrent.sizeBytes > 0 ? torrent.sizeBytes : m_config.unknownTorrentSizeBytes;
  }
  for (const QJsonValue& value : m_managed) {
    const QJsonObject allocation = value.toObject();
    if (allocation.value(QStringLiteral("clientId")).toString() != clientId) continue;
    const QString hash = allocation.value(QStringLiteral("infoHash")).toString().toLower();
    if (!hash.isEmpty() && remoteHashes.contains(hash)) continue;
    const qint64 size = allocation.value(QStringLiteral("sizeBytes")).toVariant().toLongLong();
    total += size > 0 ? size : m_config.unknownTorrentSizeBytes;
  }
  return qMax<qint64>(0, total);
}

qint64 TorrentAutomationEngine::outstandingManagedBytes(const QString& clientId) const {
  qint64 total = 0;
  for (const QJsonValue& value : m_managed) {
    const QJsonObject object = value.toObject();
    if (object.value(QStringLiteral("clientId")).toString() == clientId)
      total += qMax<qint64>(0, object.value(QStringLiteral("remainingBytes")).toVariant().toLongLong());
  }
  return total;
}

bool TorrentAutomationEngine::withinHourWindow(int startHour, int endHour) const {
  startHour = qBound(0, startHour, 23);
  endHour = qBound(0, endHour, 24);
  if (startHour == 0 && endHour == 24) return true;
  const int hour = QDateTime::currentDateTime().time().hour();
  if (startHour < endHour) return hour >= startHour && hour < endHour;
  return hour >= startHour || hour < endHour;
}

void TorrentAutomationEngine::scheduleForWindow(Job job, int startHour, const QString& reason) {
  QDateTime next = QDateTime::currentDateTime();
  next.setTime(QTime(qBound(0, startHour, 23), 0));
  if (next <= QDateTime::currentDateTime()) next = next.addDays(1);
  scheduleAt(job, next.toUTC(), reason);
}

void TorrentAutomationEngine::scheduleAt(Job job, const QDateTime& when, const QString& reason) {
  job.nextAttempt = when.toUTC();
  job.queueReason = reason;
  m_deferredJobs.append(job);
  record(QStringLiteral("scheduled"), job, {},
         tr("%1 Next check: %2").arg(reason, when.toLocalTime().toString(QStringLiteral("dd/MM/yyyy HH:mm"))));
  saveRuntime();
  armDeferredJob(job);
}

int TorrentAutomationEngine::completedCopyCount(const QString& hash) const {
  if (hash.isEmpty()) return 0;
  int copies = 0;
  for (const TorrentClientStatus& status : m_statuses)
    for (const TorrentRemoteItem& item : status.torrents)
      if (item.progress >= 1.0 && item.hash.compare(hash, Qt::CaseInsensitive) == 0) ++copies;
  return copies;
}

QDateTime TorrentAutomationEngine::lastUploadActivity(const QString& clientId, const QString& hash) const {
  for (const QJsonValue& value : m_uploadActivity) {
    const QJsonObject item = value.toObject();
    if (item.value(QStringLiteral("clientId")).toString() == clientId &&
        item.value(QStringLiteral("hash")).toString().compare(hash, Qt::CaseInsensitive) == 0)
      return QDateTime::fromString(item.value(QStringLiteral("lastUploadAt")).toString(), Qt::ISODate);
  }
  return {};
}

double TorrentAutomationEngine::cleanupScore(const TorrentRemoteItem& item, const QDateTime& now) const {
  const QDateTime basis = item.completed.isValid() ? item.completed : item.added;
  const double ageDays = basis.isValid() ? qMax<qint64>(0, basis.secsTo(now)) / 86400.0 : 0.0;
  const double sizeGiB = item.sizeBytes / (1024.0 * 1024.0 * 1024.0);
  return ageDays * 10.0 + sizeGiB * 2.0 + qMax(0.0, item.ratio) * 4.0;
}

bool TorrentAutomationEngine::circuitBreakerOpen(const QString& clientId, QString* detail) const {
  if (!m_config.circuitBreakerEnabled) return false;
  for (const QJsonValue& value : m_clientHealth) {
    const QJsonObject item = value.toObject();
    if (item.value(QStringLiteral("clientId")).toString() != clientId) continue;
    const QDateTime until = QDateTime::fromString(item.value(QStringLiteral("openUntil")).toString(), Qt::ISODate);
    if (until > QDateTime::currentDateTimeUtc()) {
      if (detail != nullptr)
        *detail = tr("Temporarily offline after repeated failures; automatic checks resume at %1.")
                    .arg(until.toLocalTime().toString(QStringLiteral("dd/MM/yyyy HH:mm")));
      return true;
    }
  }
  return false;
}

bool TorrentAutomationEngine::updateCircuitBreaker(const QString& clientId, bool success, const QString& detail) {
  if (!m_config.circuitBreakerEnabled) return true;
  int index = -1;
  QJsonObject health;
  for (int i = 0; i < m_clientHealth.size(); ++i) {
    if (m_clientHealth.at(i).toObject().value(QStringLiteral("clientId")).toString() == clientId) {
      index = i;
      health = m_clientHealth.at(i).toObject();
      break;
    }
  }
  int failures = success ? 0 : health.value(QStringLiteral("failures")).toInt() + 1;
  const bool hadOpen = health.contains(QStringLiteral("openUntil"));
  const QDateTime priorOpenUntil = QDateTime::fromString(health.value(QStringLiteral("openUntil")).toString(), Qt::ISODate);
  const bool recoveryProbe = hadOpen && priorOpenUntil <= QDateTime::currentDateTimeUtc();
  bool opened = false;
  bool recovered = false;
  bool ready = true;
  health.insert(QStringLiteral("clientId"), clientId);
  health.insert(QStringLiteral("failures"), failures);
  health.insert(QStringLiteral("detail"), detail);
  health.insert(QStringLiteral("lastCheck"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
  if (success && recoveryProbe) {
    const int recoverySuccesses = health.value(QStringLiteral("recoverySuccesses")).toInt() + 1;
    health.insert(QStringLiteral("recoverySuccesses"), recoverySuccesses);
    if (recoverySuccesses >= qMax(1, m_config.circuitBreakerRecoverySuccesses)) {
      health.remove(QStringLiteral("openUntil"));
      health.remove(QStringLiteral("recoverySuccesses"));
      recovered = true;
    }
    else ready = false;
  }
  else if (success) {
    health.remove(QStringLiteral("openUntil"));
    health.remove(QStringLiteral("recoverySuccesses"));
  }
  else if (recoveryProbe || failures >= qMax(1, m_config.circuitBreakerFailures)) {
    health.insert(QStringLiteral("openUntil"), QDateTime::currentDateTimeUtc()
                    .addSecs(qMax(1, m_config.circuitBreakerCooldownMinutes) * 60).toString(Qt::ISODate));
    health.insert(QStringLiteral("failures"), 0);
    health.insert(QStringLiteral("recoverySuccesses"), 0);
    opened = true;
  }
  if (index >= 0) m_clientHealth.replace(index, health);
  else m_clientHealth.append(health);
  if (opened || recovered) {
    Job state;
    state.title = clientId;
    record(opened ? QStringLiteral("client-sidelined") : QStringLiteral("client-recovered"), state, clientId,
           opened ? tr("Client temporarily sidelined after repeated failures; other clients remain available.")
                  : tr("Client passed consecutive recovery checks and returned to automatic routing."));
  }
  return ready && !opened;
}

void TorrentAutomationEngine::reconcileManagedState() {
  if (!m_config.reconciliationEnabled) return;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  bool changed = false;
  for (int clientIndex = 0; clientIndex < m_clients.size(); ++clientIndex) {
    TorrentClientStatus& status = m_statuses[clientIndex];
    if (!status.reachable) continue;
    const QString clientId = m_clients.at(clientIndex).id;
    QSet<QString> ledgerHashes;
    for (const QJsonValue& value : m_managed) {
      const QJsonObject allocation = value.toObject();
      if (allocation.value(QStringLiteral("clientId")).toString() != clientId) continue;
      const QString hash = allocation.value(QStringLiteral("infoHash")).toString().toLower();
      if (!hash.isEmpty()) ledgerHashes.insert(hash);
    }
    for (TorrentRemoteItem& remote : status.torrents)
      if (ledgerHashes.contains(remote.hash.toLower())) remote.managedByAutomation = true;

    for (const TorrentRemoteItem& remote : status.torrents) {
      if (!remote.hash.isEmpty() && remote.uploadBytesPerSecond > 0) {
        int activityIndex = -1;
        for (int i = 0; i < m_uploadActivity.size(); ++i) {
          const QJsonObject activity = m_uploadActivity.at(i).toObject();
          if (activity.value(QStringLiteral("clientId")).toString() == clientId &&
              activity.value(QStringLiteral("hash")).toString().compare(remote.hash, Qt::CaseInsensitive) == 0) {
            activityIndex = i;
            break;
          }
        }
        const QJsonObject activity{{QStringLiteral("clientId"), clientId},
                                   {QStringLiteral("hash"), remote.hash},
                                   {QStringLiteral("lastUploadAt"), now.toString(Qt::ISODate)}};
        if (activityIndex >= 0) m_uploadActivity.replace(activityIndex, activity);
        else m_uploadActivity.append(activity);
      }
    }

    for (int allocationIndex = m_managed.size() - 1; allocationIndex >= 0; --allocationIndex) {
      QJsonObject allocation = m_managed.at(allocationIndex).toObject();
      if (allocation.value(QStringLiteral("clientId")).toString() != clientId) continue;
      const QString hash = allocation.value(QStringLiteral("infoHash")).toString();
      if (hash.isEmpty()) continue;
      const auto found = std::find_if(status.torrents.cbegin(), status.torrents.cend(), [&hash](const TorrentRemoteItem& item) {
        return item.hash.compare(hash, Qt::CaseInsensitive) == 0;
      });
      if (found == status.torrents.cend()) {
        m_managed.removeAt(allocationIndex);
        changed = true;
        continue;
      }
      const qint64 actualSize = found->sizeBytes > 0
                                  ? found->sizeBytes
                                  : allocation.value(QStringLiteral("sizeBytes")).toVariant().toLongLong();
      const qint64 remaining = qMax<qint64>(0, qint64(double(actualSize) * (1.0 - qBound(0.0, found->progress, 1.0))));
      if (allocation.value(QStringLiteral("sizeBytes")).toVariant().toLongLong() != actualSize ||
          allocation.value(QStringLiteral("remainingBytes")).toVariant().toLongLong() != remaining) {
        allocation.insert(QStringLiteral("sizeBytes"), actualSize);
        allocation.insert(QStringLiteral("remainingBytes"), remaining);
        allocation.insert(QStringLiteral("reconciledAt"), now.toString(Qt::ISODate));
        m_managed.replace(allocationIndex, allocation);
        changed = true;
      }
    }

    for (const TorrentRemoteItem& remote : status.torrents) {
      if (!remote.managedByAutomation || remote.hash.isEmpty()) continue;
      const bool known = std::any_of(m_managed.cbegin(), m_managed.cend(), [&clientId, &remote](const QJsonValue& value) {
        const QJsonObject allocation = value.toObject();
        return allocation.value(QStringLiteral("clientId")).toString() == clientId &&
               allocation.value(QStringLiteral("infoHash")).toString().compare(remote.hash, Qt::CaseInsensitive) == 0;
      });
      if (!known) {
        const qint64 remaining = qMax<qint64>(0, qint64(double(remote.sizeBytes) *
                                                        (1.0 - qBound(0.0, remote.progress, 1.0))));
        m_managed.append(QJsonObject{{QStringLiteral("clientId"), clientId},
                                     {QStringLiteral("infoHash"), remote.hash},
                                     {QStringLiteral("sizeBytes"), remote.sizeBytes},
                                     {QStringLiteral("remainingBytes"), remaining},
                                     {QStringLiteral("added"), remote.added.toUTC().toString(Qt::ISODate)},
                                     {QStringLiteral("reconciledAt"), now.toString(Qt::ISODate)}});
        changed = true;
      }
    }
    const TorrentAutomationClientPolicy policy = m_config.policyFor(clientId);
    if (!status.liveSpace && policy.configuredCapacityBytes > 0)
      m_statuses[clientIndex].freeBytes = qMax<qint64>(0, policy.configuredCapacityBytes -
                                                          conservativeAllocatedBytes(clientId, status.torrents));
  }
  for (int candidateIndex = m_cleanupCandidates.size() - 1; candidateIndex >= 0; --candidateIndex) {
    const QJsonObject candidate = m_cleanupCandidates.at(candidateIndex).toObject();
    bool present = false;
    bool clientWasChecked = false;
    for (int clientIndex = 0; clientIndex < m_clients.size() && !present; ++clientIndex) {
      if (m_clients.at(clientIndex).id != candidate.value(QStringLiteral("clientId")).toString() ||
          !m_statuses.at(clientIndex).reachable) continue;
      clientWasChecked = true;
      present = std::any_of(m_statuses.at(clientIndex).torrents.cbegin(), m_statuses.at(clientIndex).torrents.cend(),
                            [&candidate](const TorrentRemoteItem& item) {
        return item.hash.compare(candidate.value(QStringLiteral("hash")).toString(), Qt::CaseInsensitive) == 0;
      });
    }
    if (clientWasChecked && !present) {
      m_cleanupCandidates.removeAt(candidateIndex);
      changed = true;
    }
  }
  if (changed) {
    Job summary;
    summary.title = tr("Storage reconciliation");
    record(QStringLiteral("reconciled"), summary, {},
           tr("Managed torrent sizes and remaining-space reservations were reconciled with live client lists."));
  }
  m_lastReconcile = now;
  saveRuntime();
}

void TorrentAutomationEngine::record(const QString& state,
                                     const Job& job,
                                     const QString& clientId,
                                     const QString& detail) {
  m_history.append(QJsonObject{{QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                               {QStringLiteral("state"), state}, {QStringLiteral("title"), job.title},
                               {QStringLiteral("url"), job.url}, {QStringLiteral("feedId"), job.feedId},
                               {QStringLiteral("clientId"), clientId}, {QStringLiteral("detail"), detail}});
  while (m_history.size() > m_config.historyLimit) m_history.removeFirst();
  emit activityAdded(detail);
  saveRuntime();
}

void TorrentAutomationEngine::loadRuntime() {
  const QJsonObject root = QJsonDocument::fromJson(qApp->settings()->value(RuntimeGroup, RuntimeKey).toByteArray()).object();
  for (const QJsonValue& value : root.value(QStringLiteral("processed")).toArray()) m_processed.append(value.toString());
  m_history = root.value(QStringLiteral("history")).toArray();
  m_managed = root.value(QStringLiteral("managed")).toArray();
  m_cleanupCandidates = root.value(QStringLiteral("cleanupCandidates")).toArray();
  m_uploadActivity = root.value(QStringLiteral("uploadActivity")).toArray();
  m_clientHealth = root.value(QStringLiteral("clientHealth")).toArray();
  m_lastReconcile = QDateTime::fromString(root.value(QStringLiteral("lastReconcile")).toString(), Qt::ISODate);
  for (const QJsonValue& value : root.value(QStringLiteral("pendingRetries")).toArray()) {
    const QJsonObject object = value.toObject();
    Job job;
    job.key = object.value(QStringLiteral("key")).toString();
    job.title = object.value(QStringLiteral("title")).toString();
    job.url = object.value(QStringLiteral("url")).toString();
    job.feedId = object.value(QStringLiteral("feedId")).toString();
    job.ruleName = object.value(QStringLiteral("ruleName")).toString();
    job.queueReason = object.value(QStringLiteral("queueReason")).toString();
    job.messageId = object.value(QStringLiteral("messageId")).toInt();
    for (const QJsonValue& id : object.value(QStringLiteral("allowedClientIds")).toArray()) job.allowedClientIds.append(id.toString());
    job.sizeBytes = object.value(QStringLiteral("sizeBytes")).toVariant().toLongLong();
    job.attempt = object.value(QStringLiteral("attempt")).toInt();
    job.manualApproval = object.value(QStringLiteral("manualApproval")).toBool(false);
    job.directOverride = object.value(QStringLiteral("directOverride")).toBool(false);
    job.verificationClientId = object.value(QStringLiteral("verificationClientId")).toString();
    job.nextAttempt = QDateTime::fromString(object.value(QStringLiteral("nextAttempt")).toString(), Qt::ISODate);
    if (!job.key.isEmpty() && !job.url.isEmpty()) m_deferredJobs.append(job);
  }
  for (const Job& job : std::as_const(m_deferredJobs)) armDeferredJob(job);
}

void TorrentAutomationEngine::saveRuntime() {
  QJsonArray pending;
  for (const Job& job : std::as_const(m_deferredJobs)) {
    pending.append(QJsonObject{{QStringLiteral("key"), job.key}, {QStringLiteral("title"), job.title},
                               {QStringLiteral("url"), job.url}, {QStringLiteral("feedId"), job.feedId},
                               {QStringLiteral("ruleName"), job.ruleName},
                               {QStringLiteral("queueReason"), job.queueReason},
                               {QStringLiteral("messageId"), job.messageId},
                               {QStringLiteral("allowedClientIds"), QJsonArray::fromStringList(job.allowedClientIds)},
                               {QStringLiteral("sizeBytes"), job.sizeBytes}, {QStringLiteral("attempt"), job.attempt},
                               {QStringLiteral("manualApproval"), job.manualApproval},
                               {QStringLiteral("directOverride"), job.directOverride},
                               {QStringLiteral("verificationClientId"), job.verificationClientId},
                               {QStringLiteral("nextAttempt"), job.nextAttempt.toUTC().toString(Qt::ISODate)}});
  }
  qApp->settings()->setValue(RuntimeGroup, RuntimeKey,
    QJsonDocument(QJsonObject{{QStringLiteral("processed"), QJsonArray::fromStringList(m_processed)},
                              {QStringLiteral("history"), m_history},
                              {QStringLiteral("managed"), m_managed},
                              {QStringLiteral("cleanupCandidates"), m_cleanupCandidates},
                              {QStringLiteral("uploadActivity"), m_uploadActivity},
                              {QStringLiteral("clientHealth"), m_clientHealth},
                              {QStringLiteral("lastReconcile"), m_lastReconcile.toUTC().toString(Qt::ISODate)},
                              {QStringLiteral("pendingRetries"), pending}}).toJson(QJsonDocument::Compact));
}

void TorrentAutomationEngine::notify(const QString& title, const QString& detail, bool warning) {
  if (!m_config.showNotifications || m_config.silentNotifications ||
      qApp->property("torrentSessionSilent").toBool()) return;
  qApp->showGuiMessage(Notification::Event::GeneralEvent,
                       GuiMessage(title, detail, warning ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information));
}
