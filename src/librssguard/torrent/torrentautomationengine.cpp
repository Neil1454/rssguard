// For license of this file, see <project-root-folder>/LICENSE.md.

#include "torrent/torrentautomationengine.h"

#include "core/message.h"
#include "miscellaneous/application.h"
#include "miscellaneous/settings.h"
#include "miscellaneous/notification.h"
#include "services/abstract/feed.h"
#include "torrent/torrentextractor.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSystemTrayIcon>
#include <QTimer>

#include <algorithm>
#include <limits>
#include <utility>

namespace {
  const QString RuntimeGroup = QStringLiteral("TorrentAutomation");
  const QString RuntimeKey = QStringLiteral("runtime");
  const QString AutomationTag = QStringLiteral("rssguard-auto");
  QPointer<TorrentAutomationEngine> s_engine;
}

TorrentAutomationEngine* TorrentAutomationEngine::instance(QObject* parent) {
  if (s_engine.isNull()) s_engine = new TorrentAutomationEngine(parent == nullptr ? qApp : parent);
  return s_engine;
}

void TorrentAutomationEngine::processNewArticles(const QHash<Feed*, QList<Message>>& articles, QObject* parent) {
  instance(parent)->enqueue(articles);
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

void TorrentAutomationEngine::enqueue(const QHash<Feed*, QList<Message>>& articles) {
  m_config = TorrentAutomationConfig::load(qApp->settings());
  if (!m_config.enabled) return;

  for (auto feedIt = articles.constBegin(); feedIt != articles.constEnd(); ++feedIt) {
    const QString feedId = feedIt.key() == nullptr ? QString() : feedIt.key()->customId();
    for (const Message& message : feedIt.value()) {
      QStringList allowed;
      bool matched = m_config.rules.isEmpty();
      for (const TorrentAutomationRule& rule : std::as_const(m_config.rules)) {
        if (ruleMatches(rule, feedId, message)) {
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
        job.feedId = feedId;
        job.allowedClientIds = allowed;
        if (!wasProcessed(job.key)) m_jobs.enqueue(job);
      }
    }
  }
  if (!m_jobs.isEmpty() && !m_busy) beginBatch();
}

void TorrentAutomationEngine::beginBatch() {
  m_busy = true;
  emit busyChanged(true);
  m_config = TorrentAutomationConfig::load(qApp->settings());
  m_clients.clear();
  for (const TorrentClientConfig& client : TorrentClientConfig::enabledInPriorityOrder(TorrentClientConfig::load(qApp->settings()))) {
    if (m_config.policyFor(client.id).enabled) m_clients.append(client);
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
    if (!status.liveSpace && policy.configuredCapacityBytes > 0) {
      status.totalBytes = policy.configuredCapacityBytes;
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
    const TorrentClientConfig& client = m_clients.at(i);
    const TorrentClientStatus& status = m_statuses.at(i);
    const TorrentAutomationClientPolicy policy = m_config.policyFor(client.id);
    if (!job.allowedClientIds.isEmpty() && !job.allowedClientIds.contains(client.id)) continue;
    if (!status.reachable) continue;
    if (policy.maxActiveDownloads > 0 && status.activeDownloads >= policy.maxActiveDownloads) continue;
    if (policy.maxManagedTorrents > 0) {
      int managed = 0;
      for (const TorrentRemoteItem& item : status.torrents) managed += item.managedByAutomation ? 1 : 0;
      if (managed >= policy.maxManagedTorrents) continue;
    }
    if (status.freeBytes >= 0 && status.freeBytes - job.sizeBytes < policy.minimumFreeBytes) continue;
    result.append(i);
  }
  return result;
}

int TorrentAutomationEngine::selectClient(const QList<int>& eligible) {
  if (eligible.isEmpty()) return -1;
  if (m_config.strategy == TorrentRoutingStrategy::Priority) return eligible.first();
  if (m_config.strategy == TorrentRoutingStrategy::RoundRobin) {
    const int selected = eligible.at(m_config.roundRobinCursor % eligible.size());
    ++m_config.roundRobinCursor;
    m_config.save(qApp->settings());
    return selected;
  }
  if (m_config.strategy == TorrentRoutingStrategy::Weighted) {
    int total = 0;
    for (int index : eligible) total += qMax(1, m_config.policyFor(m_clients.at(index).id).weight);
    int point = m_config.roundRobinCursor++ % total;
    m_config.save(qApp->settings());
    for (int index : eligible) {
      point -= qMax(1, m_config.policyFor(m_clients.at(index).id).weight);
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
      score = freeRatio * 1000.0 + policy.weight - status.activeDownloads * 200.0 -
              status.queuedDownloads * 80.0 - m_clients.at(index).priority * 5.0;
    }
    if (score > bestScore) { bestScore = score; best = index; }
  }
  return best;
}

void TorrentAutomationEngine::processNextJob() {
  while (!m_jobs.isEmpty() && wasProcessed(m_jobs.head().key)) m_jobs.dequeue();
  if (m_jobs.isEmpty()) { finishBatch(); return; }
  const Job job = m_jobs.dequeue();
  const QList<int> eligible = eligibleClientIndexes(job);
  const int selected = selectClient(eligible);
  if (selected < 0) {
    if (tryCleanup(job)) return;
    record(QStringLiteral("held"), job, {}, tr("No healthy client is within its configured limits."));
    notify(tr("Torrent held for retry"), tr("%1 — no client currently has safe capacity.").arg(job.title), true);
    if (job.attempt < 3) {
      Job retry = job;
      ++retry.attempt;
      QTimer::singleShot(qMax(1, m_config.retryMinutes) * 60 * 1000, this, [this, retry]() {
        m_jobs.enqueue(retry);
        if (!m_busy) beginBatch();
      });
    }
    processNextJob();
    return;
  }
  sendJob(job, selected);
}

void TorrentAutomationEngine::sendJob(const Job& job, int clientIndex) {
  TorrentClientConfig config = m_clients.at(clientIndex);
  if (!config.tags.contains(AutomationTag)) config.tags.append(AutomationTag);
  const TorrentClientStatus status = m_statuses.at(clientIndex);
  const QString decision = tr("%1 active download(s), %2 free")
                             .arg(status.activeDownloads)
                             .arg(status.freeBytes < 0 ? tr("space unknown")
                                                       : QStringLiteral("%1 GB").arg(status.freeBytes / 1000000000.0, 0, 'f', 1));
  if (m_config.dryRun) {
    record(QStringLiteral("dry-run"), job, config.id, tr("Would send to %1 (%2).").arg(config.name, decision));
    notify(tr("Torrent automation dry run"), tr("Would send “%1” to %2 — %3.").arg(job.title, config.name, decision));
    processNextJob();
    return;
  }

  TorrentClient* client = TorrentClient::create(config, this);
  connect(client, &TorrentClient::addFinished, this, [this, client, job, config, clientIndex, decision](int added, int failed, const QString& message) {
    if (added > 0 && failed == 0) {
      markProcessed(job.key);
      m_managed.append(QJsonObject{{QStringLiteral("key"), job.key},
                                   {QStringLiteral("clientId"), config.id},
                                   {QStringLiteral("sizeBytes"), job.sizeBytes},
                                   {QStringLiteral("added"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
      record(QStringLiteral("sent"), job, config.id, message);
      notify(tr("Torrent sent automatically"), tr("“%1” was sent to %2 — %3.").arg(job.title, config.name, decision));
      ++m_statuses[clientIndex].activeDownloads;
      if (m_statuses[clientIndex].freeBytes >= 0) m_statuses[clientIndex].freeBytes -= job.sizeBytes;
    }
    else {
      record(QStringLiteral("failed"), job, config.id, message);
      notify(tr("Torrent automation failed"), tr("%1: %2").arg(job.title, message), true);
    }
    client->deleteLater();
    saveRuntime();
    processNextJob();
  });
  client->addTorrents({job.url});
}

bool TorrentAutomationEngine::tryCleanup(const Job& job) {
  if (!m_config.cleanupEnabled || m_config.dryRun || m_cleanupCount >= m_config.maximumRemovalsPerRun) return false;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  for (int i = 0; i < m_clients.size(); ++i) {
    const TorrentAutomationClientPolicy policy = m_config.policyFor(m_clients.at(i).id);
    if (!policy.allowCleanup || m_statuses.at(i).freeBytes < 0 ||
        m_statuses.at(i).freeBytes >= policy.minimumFreeBytes) continue;
    QList<TorrentRemoteItem> candidates;
    for (const TorrentRemoteItem& item : m_statuses.at(i).torrents) {
      if (!item.managedByAutomation || item.progress < 1.0 || item.downloading || item.hash.isEmpty()) continue;
      if (item.ratio < m_config.minimumRatio) continue;
      if (item.completed.isValid() && item.completed.secsTo(now) < qint64(m_config.minimumSeedHours) * 3600) continue;
      if (item.lastActivity.isValid() && item.lastActivity.secsTo(now) < qint64(m_config.minimumInactiveHours) * 3600) continue;
      candidates.append(item);
    }
    if (candidates.isEmpty()) continue;
    std::sort(candidates.begin(), candidates.end(), [](const TorrentRemoteItem& a, const TorrentRemoteItem& b) {
      return a.completed < b.completed;
    });
    const TorrentRemoteItem victim = candidates.first();
    if (m_config.cleanupRequireConfirmation) {
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
}

void TorrentAutomationEngine::saveRuntime() {
  qApp->settings()->setValue(RuntimeGroup, RuntimeKey,
    QJsonDocument(QJsonObject{{QStringLiteral("processed"), QJsonArray::fromStringList(m_processed)},
                              {QStringLiteral("history"), m_history},
                              {QStringLiteral("managed"), m_managed}}).toJson(QJsonDocument::Compact));
}

void TorrentAutomationEngine::notify(const QString& title, const QString& detail, bool warning) {
  if (!m_config.showNotifications) return;
  qApp->showGuiMessage(Notification::Event::GeneralEvent,
                       GuiMessage(title, detail, warning ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information));
}
