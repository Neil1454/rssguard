// For license of this file, see <project-root-folder>/LICENSE.md.

#include "torrent/torrentautomationengine.h"

#include "core/message.h"
#include "miscellaneous/application.h"
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
#include <QSystemTrayIcon>
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
  if (m_lastArticles.isEmpty()) {
    Job summary;
    summary.title = tr("Manual dry run");
    record(QStringLiteral("DRY RUN"), summary, {},
           tr("No newly fetched RSS items are available. Refresh the feeds, then run the test again."));
    return;
  }
  enqueue(m_lastArticles, true);
}

TorrentAutomationEngine::TorrentAutomationEngine(QObject* parent) : QObject(parent) { loadRuntime(); }

bool TorrentAutomationEngine::busy() const { return m_busy; }

QStringList TorrentAutomationEngine::recentActivity() const {
  QStringList result;
  for (int index = m_history.size() - 1; index >= 0; --index) {
    const QJsonObject item = m_history.at(index).toObject();
    result.append(QStringLiteral("%1 — %2 — %3")
                    .arg(item.value(QStringLiteral("time")).toString(),
                         item.value(QStringLiteral("state")).toString(),
                         item.value(QStringLiteral("detail")).toString()));
    if (result.size() >= 100) break;
  }
  return result;
}

QString TorrentAutomationEngine::jobKey(const QString& url) const {
  QString normalized = url.trimmed();
  if (normalized.startsWith(QStringLiteral("magnet:"), Qt::CaseInsensitive)) normalized = normalized.toLower();
  return QString::fromLatin1(QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool TorrentAutomationEngine::ruleMatches(const TorrentAutomationRule& rule,
                                           const QString& feedId,
                                           const Message& message) const {
  if (!rule.enabled || (!rule.feedIds.isEmpty() && !rule.feedIds.contains(feedId))) return false;
  const QString searchable = message.m_title + QLatin1Char('\n') + message.m_contents;
  if (!rule.requiredText.isEmpty() && !searchable.contains(rule.requiredText, Qt::CaseInsensitive)) return false;
  if (!rule.excludedText.isEmpty() && searchable.contains(rule.excludedText, Qt::CaseInsensitive)) return false;
  if (!rule.titleRegularExpression.isEmpty()) {
    const QRegularExpression expression(rule.titleRegularExpression, QRegularExpression::CaseInsensitiveOption);
    if (!expression.isValid() || !expression.match(message.m_title).hasMatch()) return false;
  }
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
      QStringList allowed;
      bool matched = m_config.rules.isEmpty();
      for (const TorrentAutomationRule& rule : std::as_const(m_config.rules)) {
        if (ruleMatches(rule, messageFeedId, message)) {
          matched = true;
          allowed = rule.clientIds;
          break;
        }
      }
      if (!matched) continue;
      for (const QString& url : TorrentExtractor::extract(message)) {
        Job job;
        job.key = jobKey(url);
        job.title = message.m_title;
        job.url = url;
        job.feedId = messageFeedId;
        job.messageId = message.m_id;
        job.allowedClientIds = allowed;
        job.sizeBytes = torrentSizeHint(url, m_config.unknownTorrentSizeBytes);
        job.manualApproval = manualApproval;
        if (!wasProcessed(job.key)) m_jobs.enqueue(job);
      }
    }
  }
  if (!m_jobs.isEmpty() && !m_busy) beginBatch();
  else if (forceDryRun && !m_busy) {
    Job summary;
    summary.title = tr("Manual dry run");
    record(QStringLiteral("DRY RUN"), summary, {},
           tr("No eligible unprocessed torrent items matched the current RSS rules."));
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
  m_queryIndex = 0;
  m_cleanupCount = 0;
  if (m_clients.isEmpty()) {
    notify(tr("Torrent automation"), tr("No enabled torrent clients participate in automation."), true);
    finishBatch();
    return;
  }
  queryNextClient();
}

void TorrentAutomationEngine::queryNextClient() {
  if (m_queryIndex >= m_clients.size()) {
    processNextJob();
    return;
  }
  const int index = m_queryIndex++;
  TorrentClient* client = TorrentClient::create(m_clients.at(index), this);
  connect(client, &TorrentClient::statusFinished, this, [this, client, index](TorrentClientStatus status) {
    const TorrentAutomationClientPolicy policy = m_config.policyFor(m_clients.at(index).id);
    if (status.totalBytes <= 0 && policy.configuredCapacityBytes > 0)
      status.totalBytes = policy.configuredCapacityBytes;
    if (status.freeBytes < 0 && policy.configuredCapacityBytes > 0) {
      status.freeBytes = qMax<qint64>(0, policy.configuredCapacityBytes - estimatedManagedBytes(policy.clientId));
    }
    m_statuses[index] = status;
    client->deleteLater();
    queryNextClient();
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
  while (!m_jobs.isEmpty() && !m_jobs.head().directOverride && wasProcessed(m_jobs.head().key)) m_jobs.dequeue();
  if (m_jobs.isEmpty()) { finishBatch(); return; }
  const Job job = m_jobs.dequeue();
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
  if (selected < 0) {
    if (tryCleanup(job)) return;
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
  sendJob(job, selected);
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
    record(QStringLiteral("DRY RUN"), job, config.id, tr("Would send to %1 (%2).").arg(config.name, decision));
    notify(tr("Torrent automation dry run"), tr("Would send “%1” to %2 — %3.").arg(job.title, config.name, decision));
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
                                   {QStringLiteral("sizeBytes"), job.sizeBytes},
                                   {QStringLiteral("added"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
      record(QStringLiteral("sent"), job, config.id, message);
      const bool showDirectSuccess = qApp->settings()->value(QStringLiteral("TorrentClients"),
                                                             QStringLiteral("showSuccessNotifications"), true).toBool();
      if (!job.directOverride || showDirectSuccess)
        notify(job.directOverride ? tr("Torrent sent") : tr("Torrent sent automatically"),
               tr("“%1” was sent to %2 — %3.").arg(job.title, config.name, decision));
      ++m_statuses[clientIndex].activeDownloads;
      if (m_statuses[clientIndex].freeBytes >= 0) m_statuses[clientIndex].freeBytes -= job.sizeBytes;
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

bool TorrentAutomationEngine::tryCleanup(const Job& job) {
  const int removalLimit = m_config.maximumRemovalsEnabled ? m_config.maximumRemovalsPerRun : 25;
  if (!m_config.cleanupEnabled || m_cleanupCount >= removalLimit) return false;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  for (int i = 0; i < m_clients.size(); ++i) {
    const TorrentAutomationClientPolicy policy = m_config.policyFor(m_clients.at(i).id);
    const TorrentClientStatus& clientStatus = m_statuses.at(i);
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
    for (const TorrentRemoteItem& item : clientStatus.torrents) {
      if (!item.managedByAutomation || item.progress < 1.0 || item.downloading || item.hash.isEmpty()) continue;
      if (m_config.protectUploadingEnabled &&
          ((item.uploadBytesPerSecond < 0 && m_config.protectWhenSpeedUnknown) ||
           item.uploadBytesPerSecond >= m_config.protectUploadBytesPerSecond)) {
        ++protectedUploads;
        continue;
      }
      if (m_config.minimumRatioEnabled && item.ratio < m_config.minimumRatio) continue;
      if (m_config.minimumSeedHoursEnabled &&
          (!item.completed.isValid() || item.completed.secsTo(now) < qint64(m_config.minimumSeedHours) * 3600)) continue;
      if (m_config.minimumInactiveHoursEnabled &&
          (!item.lastActivity.isValid() || item.lastActivity.secsTo(now) < qint64(m_config.minimumInactiveHours) * 3600)) continue;
      candidates.append(item);
    }
    if (candidates.isEmpty()) {
      if (protectedUploads > 0)
        record(QStringLiteral("protected"), job, m_clients.at(i).id,
               tr("%1 old torrent(s) were retained because they are actively uploading or their speed is unavailable.")
                 .arg(protectedUploads));
      continue;
    }
    std::sort(candidates.begin(), candidates.end(), [](const TorrentRemoteItem& a, const TorrentRemoteItem& b) {
      const QDateTime aDate = a.completed.isValid() ? a.completed : a.added;
      const QDateTime bDate = b.completed.isValid() ? b.completed : b.added;
      return aDate < bDate;
    });
    const TorrentRemoteItem victim = candidates.first();
    if (m_config.dryRun) {
      record(QStringLiteral("DRY RUN"), job, m_clients.at(i).id,
             tr("Would remove %1 from %2 to recover approximately %3 GB; no changes made.")
               .arg(victim.name, m_clients.at(i).name)
               .arg(victim.sizeBytes / 1000000000.0, 0, 'f', 1));
      processNextJob();
      return true;
    }
    const bool noEligibilityFilters = !m_config.minimumSeedHoursEnabled && !m_config.minimumRatioEnabled &&
                                      !m_config.minimumInactiveHoursEnabled;
    if (m_config.cleanupRequireConfirmation || m_config.deleteData || noEligibilityFilters) {
      const auto answer = QMessageBox::warning(qApp->mainFormWidget(), tr("Confirm automatic torrent cleanup"),
        tr("Remove “%1” from %2%3 to make space for “%4”?\n\nThis action cannot be undone.")
          .arg(victim.name, m_clients.at(i).name, m_config.deleteData ? tr(" and delete its downloaded data") : QString(), job.title),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
      if (answer != QMessageBox::Yes) continue;
    }
    TorrentClient* client = TorrentClient::create(m_clients.at(i), this);
    connect(client, &TorrentClient::removeFinished, this, [this, client, job, i, victim](bool success, const QString& message) {
      if (success) {
        ++m_cleanupCount;
        m_statuses[i].freeBytes += victim.sizeBytes;
        record(QStringLiteral("cleanup"), job, m_clients.at(i).id,
               tr("Removed %1 and recovered approximately %2 GB.").arg(victim.name).arg(victim.sizeBytes / 1000000000.0, 0, 'f', 1));
        notify(tr("Torrent automation cleanup"), tr("Removed “%1” from %2 to make room for “%3”.")
                                                      .arg(victim.name, m_clients.at(i).name, job.title));
      }
      else notify(tr("Torrent cleanup failed"), message, true);
      client->deleteLater();
      m_jobs.prepend(job);
      processNextJob();
    });
    client->removeTorrent(victim.hash, m_config.deleteData);
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

qint64 TorrentAutomationEngine::estimatedManagedBytes(const QString& clientId) const {
  qint64 total = 0;
  for (const QJsonValue& value : m_managed) {
    const QJsonObject object = value.toObject();
    if (object.value(QStringLiteral("clientId")).toString() == clientId)
      total += object.value(QStringLiteral("sizeBytes")).toVariant().toLongLong();
  }
  return total;
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
  for (const QJsonValue& value : root.value(QStringLiteral("pendingRetries")).toArray()) {
    const QJsonObject object = value.toObject();
    Job job;
    job.key = object.value(QStringLiteral("key")).toString();
    job.title = object.value(QStringLiteral("title")).toString();
    job.url = object.value(QStringLiteral("url")).toString();
    job.feedId = object.value(QStringLiteral("feedId")).toString();
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
                              {QStringLiteral("pendingRetries"), pending}}).toJson(QJsonDocument::Compact));
}

void TorrentAutomationEngine::notify(const QString& title, const QString& detail, bool warning) {
  if (!m_config.showNotifications) return;
  qApp->showGuiMessage(Notification::Event::GeneralEvent,
                       GuiMessage(title, detail, warning ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information));
}
