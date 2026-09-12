// For license of this file, see <project-root-folder>/LICENSE.md.

#include "torrent/torrentautomationconfig.h"

#include "miscellaneous/settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QUuid>

namespace {
  const QString Group = QStringLiteral("TorrentAutomation");
  const QString Key = QStringLiteral("configuration");
}

TorrentAutomationClientPolicy TorrentAutomationConfig::policyFor(const QString& clientId) const {
  for (const TorrentAutomationClientPolicy& policy : clients)
    if (policy.clientId == clientId) return policy;
  TorrentAutomationClientPolicy policy;
  policy.clientId = clientId;
  return policy;
}

QString TorrentAutomationConfig::strategyName(TorrentRoutingStrategy strategy) {
  switch (strategy) {
    case TorrentRoutingStrategy::Priority: return QObject::tr("Priority order");
    case TorrentRoutingStrategy::LeastBusy: return QObject::tr("Least busy");
    case TorrentRoutingStrategy::MostFreeSpace: return QObject::tr("Most free space");
    case TorrentRoutingStrategy::RoundRobin: return QObject::tr("Even distribution (round robin)");
    case TorrentRoutingStrategy::Weighted: return QObject::tr("Weighted distribution");
    case TorrentRoutingStrategy::Balanced: return QObject::tr("Balanced (recommended)");
  }
  return QObject::tr("Balanced (recommended)");
}

TorrentAutomationConfig TorrentAutomationConfig::load(Settings* settings) {
  TorrentAutomationConfig config;
  const QJsonObject root = QJsonDocument::fromJson(settings->value(Group, Key).toByteArray()).object();
  if (root.isEmpty()) return config;

  config.enabled = root.value(QStringLiteral("enabled")).toBool(false);
  config.dryRun = root.value(QStringLiteral("dryRun")).toBool(true);
  config.showNotifications = root.value(QStringLiteral("showNotifications")).toBool(true);
  config.strategy = static_cast<TorrentRoutingStrategy>(root.value(QStringLiteral("strategy")).toInt(5));
  config.retryMinutes = root.value(QStringLiteral("retryMinutes")).toInt(15);
  config.historyLimit = root.value(QStringLiteral("historyLimit")).toInt(500);
  config.roundRobinCursor = root.value(QStringLiteral("roundRobinCursor")).toInt(0);
  config.cleanupEnabled = root.value(QStringLiteral("cleanupEnabled")).toBool(false);
  config.deleteData = root.value(QStringLiteral("deleteData")).toBool(false);
  config.cleanupRequireConfirmation = root.value(QStringLiteral("cleanupRequireConfirmation")).toBool(true);
  config.minimumSeedHours = root.value(QStringLiteral("minimumSeedHours")).toInt(168);
  config.minimumRatio = root.value(QStringLiteral("minimumRatio")).toDouble(1.0);
  config.minimumInactiveHours = root.value(QStringLiteral("minimumInactiveHours")).toInt(24);
  config.maximumRemovalsPerRun = root.value(QStringLiteral("maximumRemovalsPerRun")).toInt(1);
  config.cleanupStopFreeBytes = root.value(QStringLiteral("cleanupStopFreeBytes")).toVariant().toLongLong();
  if (config.cleanupStopFreeBytes <= 0) config.cleanupStopFreeBytes = 40LL * 1024 * 1024 * 1024;

  for (const QJsonValue& value : root.value(QStringLiteral("clients")).toArray()) {
    const QJsonObject object = value.toObject();
    TorrentAutomationClientPolicy policy;
    policy.clientId = object.value(QStringLiteral("clientId")).toString();
    policy.enabled = object.value(QStringLiteral("enabled")).toBool(true);
    policy.weight = object.value(QStringLiteral("weight")).toInt(100);
    policy.maxActiveDownloads = object.value(QStringLiteral("maxActiveDownloads")).toInt(3);
    policy.maxManagedTorrents = object.value(QStringLiteral("maxManagedTorrents")).toInt(0);
    policy.minimumFreeBytes = object.value(QStringLiteral("minimumFreeBytes")).toVariant().toLongLong();
    policy.configuredCapacityBytes = object.value(QStringLiteral("configuredCapacityBytes")).toVariant().toLongLong();
    policy.allowCleanup = object.value(QStringLiteral("allowCleanup")).toBool(false);
    if (!policy.clientId.isEmpty()) config.clients.append(policy);
  }

  for (const QJsonValue& value : root.value(QStringLiteral("rules")).toArray()) {
    const QJsonObject object = value.toObject();
    TorrentAutomationRule rule;
    rule.id = object.value(QStringLiteral("id")).toString();
    rule.name = object.value(QStringLiteral("name")).toString();
    rule.enabled = object.value(QStringLiteral("enabled")).toBool(true);
    for (const QJsonValue& id : object.value(QStringLiteral("feedIds")).toArray()) rule.feedIds.append(id.toString());
    rule.requiredText = object.value(QStringLiteral("requiredText")).toString();
    rule.excludedText = object.value(QStringLiteral("excludedText")).toString();
    rule.titleRegularExpression = object.value(QStringLiteral("titleRegularExpression")).toString();
    rule.minimumSizeBytes = object.value(QStringLiteral("minimumSizeBytes")).toVariant().toLongLong();
    rule.maximumSizeBytes = object.value(QStringLiteral("maximumSizeBytes")).toVariant().toLongLong();
    for (const QJsonValue& id : object.value(QStringLiteral("clientIds")).toArray()) rule.clientIds.append(id.toString());
    if (!rule.id.isEmpty()) config.rules.append(rule);
  }
  return config;
}

void TorrentAutomationConfig::save(Settings* settings) const {
  QJsonObject root;
  root.insert(QStringLiteral("enabled"), enabled);
  root.insert(QStringLiteral("dryRun"), dryRun);
  root.insert(QStringLiteral("showNotifications"), showNotifications);
  root.insert(QStringLiteral("strategy"), static_cast<int>(strategy));
  root.insert(QStringLiteral("retryMinutes"), retryMinutes);
  root.insert(QStringLiteral("historyLimit"), historyLimit);
  root.insert(QStringLiteral("roundRobinCursor"), roundRobinCursor);
  root.insert(QStringLiteral("cleanupEnabled"), cleanupEnabled);
  root.insert(QStringLiteral("deleteData"), deleteData);
  root.insert(QStringLiteral("cleanupRequireConfirmation"), cleanupRequireConfirmation);
  root.insert(QStringLiteral("minimumSeedHours"), minimumSeedHours);
  root.insert(QStringLiteral("minimumRatio"), minimumRatio);
  root.insert(QStringLiteral("minimumInactiveHours"), minimumInactiveHours);
  root.insert(QStringLiteral("maximumRemovalsPerRun"), maximumRemovalsPerRun);
  root.insert(QStringLiteral("cleanupStopFreeBytes"), cleanupStopFreeBytes);

  QJsonArray policies;
  for (const TorrentAutomationClientPolicy& policy : clients) {
    policies.append(QJsonObject{{QStringLiteral("clientId"), policy.clientId},
                                {QStringLiteral("enabled"), policy.enabled},
                                {QStringLiteral("weight"), policy.weight},
                                {QStringLiteral("maxActiveDownloads"), policy.maxActiveDownloads},
                                {QStringLiteral("maxManagedTorrents"), policy.maxManagedTorrents},
                                {QStringLiteral("minimumFreeBytes"), policy.minimumFreeBytes},
                                {QStringLiteral("configuredCapacityBytes"), policy.configuredCapacityBytes},
                                {QStringLiteral("allowCleanup"), policy.allowCleanup}});
  }
  root.insert(QStringLiteral("clients"), policies);

  QJsonArray ruleArray;
  for (TorrentAutomationRule rule : rules) {
    if (rule.id.isEmpty()) rule.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ruleArray.append(QJsonObject{{QStringLiteral("id"), rule.id},
                                 {QStringLiteral("name"), rule.name},
                                 {QStringLiteral("enabled"), rule.enabled},
                                 {QStringLiteral("feedIds"), QJsonArray::fromStringList(rule.feedIds)},
                                 {QStringLiteral("requiredText"), rule.requiredText},
                                 {QStringLiteral("excludedText"), rule.excludedText},
                                 {QStringLiteral("titleRegularExpression"), rule.titleRegularExpression},
                                 {QStringLiteral("minimumSizeBytes"), rule.minimumSizeBytes},
                                 {QStringLiteral("maximumSizeBytes"), rule.maximumSizeBytes},
                                 {QStringLiteral("clientIds"), QJsonArray::fromStringList(rule.clientIds)}});
  }
  root.insert(QStringLiteral("rules"), ruleArray);
  settings->setValue(Group, Key, QJsonDocument(root).toJson(QJsonDocument::Compact));
}
