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
    case TorrentRoutingStrategy::Weighted: return QObject::tr("Priority-biased distribution");
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
  config.paused = root.value(QStringLiteral("paused")).toBool(false);
  config.silentNotifications = root.value(QStringLiteral("silentNotifications")).toBool(false);
  config.ignoreInitialFeedBatch = root.value(QStringLiteral("ignoreInitialFeedBatch")).toBool(false);
  config.markInitialFeedBatchRead = root.value(QStringLiteral("markInitialFeedBatchRead")).toBool(false);
  config.exclusiveModeEnabled = root.value(QStringLiteral("exclusiveModeEnabled")).toBool(false);
  config.exclusiveSleepMinutes = qBound(1, root.value(QStringLiteral("exclusiveSleepMinutes")).toInt(60), 10080);
  config.exclusiveFreshnessMinutes = qBound(0, root.value(QStringLiteral("exclusiveFreshnessMinutes")).toInt(3), 1440);
  config.exclusiveMonitoringMinutes = qBound(1, root.value(QStringLiteral("exclusiveMonitoringMinutes")).toInt(15), 1440);
  config.exclusivePollMinutes = qBound(1, root.value(QStringLiteral("exclusivePollMinutes")).toInt(1), 60);
  config.exclusiveBatchSize = qBound(1, root.value(QStringLiteral("exclusiveBatchSize")).toInt(5), 1000);
  config.exclusiveSendPartialBatch = root.value(QStringLiteral("exclusiveSendPartialBatch")).toBool(true);
  config.notificationDurationSeconds = qMax(0, root.value(QStringLiteral("notificationDurationSeconds")).toInt(0));
  config.maximumConsecutiveAssignments = qMax(1, root.value(QStringLiteral("maximumConsecutiveAssignments")).toInt(1));
  config.speedDisplayUnit = root.value(QStringLiteral("speedDisplayUnit")).toString(QStringLiteral("MiB/s"));
  config.strategy = static_cast<TorrentRoutingStrategy>(root.value(QStringLiteral("strategy")).toInt(5));
  config.retryMinutes = root.value(QStringLiteral("retryMinutes")).toInt(1);
  config.retryEnabled = root.value(QStringLiteral("retryEnabled")).toBool(true);
  config.retryAttempts = root.value(QStringLiteral("retryAttempts")).toInt(3);
  config.retryInitialSeconds = root.value(QStringLiteral("retryInitialSeconds")).toInt(config.retryMinutes * 60);
  config.retryMaximumSeconds = root.value(QStringLiteral("retryMaximumSeconds")).toInt(900);
  config.retryExponentialBackoff = root.value(QStringLiteral("retryExponentialBackoff")).toBool(true);
  config.requestTimeoutSeconds = root.value(QStringLiteral("requestTimeoutSeconds")).toInt(15);
  config.historyLimit = root.value(QStringLiteral("historyLimit")).toInt(500);
  config.storageDisplayUnit = root.value(QStringLiteral("storageDisplayUnit")).toString(QStringLiteral("GiB"));
  if (config.storageDisplayUnit != QStringLiteral("MiB") &&
      config.storageDisplayUnit != QStringLiteral("GiB") &&
      config.storageDisplayUnit != QStringLiteral("TiB"))
    config.storageDisplayUnit = QStringLiteral("GiB");
  config.roundRobinCursor = root.value(QStringLiteral("roundRobinCursor")).toInt(0);
  config.unknownTorrentSizeBytes = root.value(QStringLiteral("unknownTorrentSizeBytes"))
                                     .toVariant().toLongLong();
  if (config.unknownTorrentSizeBytes <= 0) config.unknownTorrentSizeBytes = 10LL * 1024 * 1024 * 1024;
  config.reconciliationEnabled = root.value(QStringLiteral("reconciliationEnabled")).toBool(true);
  config.reserveRemainingBytes = root.value(QStringLiteral("reserveRemainingBytes")).toBool(true);
  config.preventDuplicateAcrossClients = root.value(QStringLiteral("preventDuplicateAcrossClients")).toBool(true);
  config.reconciliationMinutes = root.value(QStringLiteral("reconciliationMinutes")).toInt(30);
  config.circuitBreakerEnabled = root.value(QStringLiteral("circuitBreakerEnabled")).toBool(true);
  config.circuitBreakerFailures = root.value(QStringLiteral("circuitBreakerFailures")).toInt(3);
  config.circuitBreakerCooldownMinutes = root.value(QStringLiteral("circuitBreakerCooldownMinutes")).toInt(15);
  config.circuitBreakerRecoverySuccesses = root.value(QStringLiteral("circuitBreakerRecoverySuccesses")).toInt(2);
  config.scheduleEnabled = root.value(QStringLiteral("scheduleEnabled")).toBool(false);
  config.scheduleStartHour = root.value(QStringLiteral("scheduleStartHour")).toInt(0);
  config.scheduleEndHour = root.value(QStringLiteral("scheduleEndHour")).toInt(24);
  config.cleanupEnabled = root.value(QStringLiteral("cleanupEnabled")).toBool(false);
  config.deleteData = root.value(QStringLiteral("deleteData")).toBool(false);
  config.cleanupRequireConfirmation = root.value(QStringLiteral("cleanupRequireConfirmation")).toBool(true);
  config.cleanupConfirmationSeconds = qBound(60, root.value(QStringLiteral("cleanupConfirmationSeconds")).toInt(60), 3600);
  config.cleanupIncludeUnmanaged = root.value(QStringLiteral("cleanupIncludeUnmanaged")).toBool(false);
  for (const QJsonValue& value : root.value(QStringLiteral("protectedTorrentHashes")).toArray())
    config.protectedTorrentHashes.append(value.toString().toLower());
  config.maximumRetentionEnabled = root.value(QStringLiteral("maximumRetentionEnabled")).toBool(false);
  config.maximumRetentionHours = qMax(1, root.value(QStringLiteral("maximumRetentionHours")).toInt(720));
  config.maximumRetentionStrict = root.value(QStringLiteral("maximumRetentionStrict")).toBool(true);
  config.minimumSeedHoursEnabled = root.value(QStringLiteral("minimumSeedHoursEnabled")).toBool(true);
  config.minimumSeedHours = root.value(QStringLiteral("minimumSeedHours")).toInt(168);
  config.minimumRatioEnabled = root.value(QStringLiteral("minimumRatioEnabled")).toBool(true);
  config.minimumRatio = root.value(QStringLiteral("minimumRatio")).toDouble(1.0);
  config.minimumInactiveHoursEnabled = root.value(QStringLiteral("minimumInactiveHoursEnabled")).toBool(true);
  config.minimumInactiveHours = root.value(QStringLiteral("minimumInactiveHours")).toInt(24);
  config.maximumRemovalsEnabled = root.value(QStringLiteral("maximumRemovalsEnabled")).toBool(true);
  config.maximumRemovalsPerRun = root.value(QStringLiteral("maximumRemovalsPerRun")).toInt(1);
  config.cleanupStopFreeEnabled = root.value(QStringLiteral("cleanupStopFreeEnabled")).toBool(true);
  config.cleanupStopFreeBytes = root.value(QStringLiteral("cleanupStopFreeBytes")).toVariant().toLongLong();
  if (config.cleanupStopFreeBytes <= 0) config.cleanupStopFreeBytes = 40LL * 1024 * 1024 * 1024;
  config.protectUploadingEnabled = root.value(QStringLiteral("protectUploadingEnabled")).toBool(true);
  config.protectUploadBytesPerSecond = root.value(QStringLiteral("protectUploadBytesPerSecond")).toVariant().toLongLong();
  if (config.protectUploadBytesPerSecond <= 0) config.protectUploadBytesPerSecond = 256LL * 1024;
  config.protectWhenSpeedUnknown = root.value(QStringLiteral("protectWhenSpeedUnknown")).toBool(true);
  config.protectRecentUploadHours = root.value(QStringLiteral("protectRecentUploadHours")).toInt(24);
  config.cleanupGraceEnabled = root.value(QStringLiteral("cleanupGraceEnabled")).toBool(true);
  config.cleanupGraceHours = root.value(QStringLiteral("cleanupGraceHours")).toInt(24);
  config.smartCleanupOrder = root.value(QStringLiteral("smartCleanupOrder")).toBool(true);
  config.minimumCopiesEnabled = root.value(QStringLiteral("minimumCopiesEnabled")).toBool(false);
  config.minimumCopiesAcrossClients = root.value(QStringLiteral("minimumCopiesAcrossClients")).toInt(1);
  for (const QJsonValue& value : root.value(QStringLiteral("protectedTags")).toArray())
    config.protectedTags.append(value.toString());
  for (const QJsonValue& value : root.value(QStringLiteral("protectedTrackerTerms")).toArray())
    config.protectedTrackerTerms.append(value.toString());
  config.cleanupScheduleEnabled = root.value(QStringLiteral("cleanupScheduleEnabled")).toBool(false);
  config.cleanupScheduleStartHour = root.value(QStringLiteral("cleanupScheduleStartHour")).toInt(0);
  config.cleanupScheduleEndHour = root.value(QStringLiteral("cleanupScheduleEndHour")).toInt(24);
  config.cleanupBatchPercent = root.value(QStringLiteral("cleanupBatchPercent")).toDouble(5.0);

  for (const QJsonValue& value : root.value(QStringLiteral("clients")).toArray()) {
    const QJsonObject object = value.toObject();
    TorrentAutomationClientPolicy policy;
    policy.clientId = object.value(QStringLiteral("clientId")).toString();
    policy.enabled = object.value(QStringLiteral("enabled")).toBool(true);
    policy.priority = object.value(QStringLiteral("priority")).toInt(1);
    if (!object.contains(QStringLiteral("priority")) && object.contains(QStringLiteral("weight")))
      policy.priority = 1;
    policy.maxActiveDownloads = object.value(QStringLiteral("maxActiveDownloads")).toInt(3);
    policy.maxManagedTorrents = object.value(QStringLiteral("maxManagedTorrents")).toInt(0);
    policy.minimumFreeBytes = object.value(QStringLiteral("minimumFreeBytes")).toVariant().toLongLong();
    policy.targetFreePercent = object.value(QStringLiteral("targetFreePercent")).toDouble(0.0);
    policy.configuredCapacityBytes = object.value(QStringLiteral("configuredCapacityBytes")).toVariant().toLongLong();
    policy.maximumDownloadBytesPerSecond = object.value(QStringLiteral("maximumDownloadBytesPerSecond")).toVariant().toLongLong();
    policy.requestTimeoutSeconds = object.value(QStringLiteral("requestTimeoutSeconds")).toInt(0);
    policy.retryAttempts = object.value(QStringLiteral("retryAttempts")).toInt(-1);
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
  root.insert(QStringLiteral("paused"), paused);
  root.insert(QStringLiteral("silentNotifications"), silentNotifications);
  root.insert(QStringLiteral("ignoreInitialFeedBatch"), ignoreInitialFeedBatch);
  root.insert(QStringLiteral("markInitialFeedBatchRead"), markInitialFeedBatchRead);
  root.insert(QStringLiteral("exclusiveModeEnabled"), exclusiveModeEnabled);
  root.insert(QStringLiteral("exclusiveSleepMinutes"), exclusiveSleepMinutes);
  root.insert(QStringLiteral("exclusiveFreshnessMinutes"), exclusiveFreshnessMinutes);
  root.insert(QStringLiteral("exclusiveMonitoringMinutes"), exclusiveMonitoringMinutes);
  root.insert(QStringLiteral("exclusivePollMinutes"), exclusivePollMinutes);
  root.insert(QStringLiteral("exclusiveBatchSize"), exclusiveBatchSize);
  root.insert(QStringLiteral("exclusiveSendPartialBatch"), exclusiveSendPartialBatch);
  root.insert(QStringLiteral("notificationDurationSeconds"), notificationDurationSeconds);
  root.insert(QStringLiteral("maximumConsecutiveAssignments"), maximumConsecutiveAssignments);
  root.insert(QStringLiteral("speedDisplayUnit"), speedDisplayUnit);
  root.insert(QStringLiteral("strategy"), static_cast<int>(strategy));
  root.insert(QStringLiteral("retryMinutes"), retryMinutes);
  root.insert(QStringLiteral("retryEnabled"), retryEnabled);
  root.insert(QStringLiteral("retryAttempts"), retryAttempts);
  root.insert(QStringLiteral("retryInitialSeconds"), retryInitialSeconds);
  root.insert(QStringLiteral("retryMaximumSeconds"), retryMaximumSeconds);
  root.insert(QStringLiteral("retryExponentialBackoff"), retryExponentialBackoff);
  root.insert(QStringLiteral("requestTimeoutSeconds"), requestTimeoutSeconds);
  root.insert(QStringLiteral("historyLimit"), historyLimit);
  root.insert(QStringLiteral("storageDisplayUnit"), storageDisplayUnit);
  root.insert(QStringLiteral("roundRobinCursor"), roundRobinCursor);
  root.insert(QStringLiteral("unknownTorrentSizeBytes"), unknownTorrentSizeBytes);
  root.insert(QStringLiteral("reconciliationEnabled"), reconciliationEnabled);
  root.insert(QStringLiteral("reserveRemainingBytes"), reserveRemainingBytes);
  root.insert(QStringLiteral("preventDuplicateAcrossClients"), preventDuplicateAcrossClients);
  root.insert(QStringLiteral("reconciliationMinutes"), reconciliationMinutes);
  root.insert(QStringLiteral("circuitBreakerEnabled"), circuitBreakerEnabled);
  root.insert(QStringLiteral("circuitBreakerFailures"), circuitBreakerFailures);
  root.insert(QStringLiteral("circuitBreakerCooldownMinutes"), circuitBreakerCooldownMinutes);
  root.insert(QStringLiteral("circuitBreakerRecoverySuccesses"), circuitBreakerRecoverySuccesses);
  root.insert(QStringLiteral("scheduleEnabled"), scheduleEnabled);
  root.insert(QStringLiteral("scheduleStartHour"), scheduleStartHour);
  root.insert(QStringLiteral("scheduleEndHour"), scheduleEndHour);
  root.insert(QStringLiteral("cleanupEnabled"), cleanupEnabled);
  root.insert(QStringLiteral("deleteData"), deleteData);
  root.insert(QStringLiteral("cleanupRequireConfirmation"), cleanupRequireConfirmation);
  root.insert(QStringLiteral("cleanupConfirmationSeconds"), cleanupConfirmationSeconds);
  root.insert(QStringLiteral("cleanupIncludeUnmanaged"), cleanupIncludeUnmanaged);
  root.insert(QStringLiteral("protectedTorrentHashes"), QJsonArray::fromStringList(protectedTorrentHashes));
  root.insert(QStringLiteral("maximumRetentionEnabled"), maximumRetentionEnabled);
  root.insert(QStringLiteral("maximumRetentionHours"), maximumRetentionHours);
  root.insert(QStringLiteral("maximumRetentionStrict"), maximumRetentionStrict);
  root.insert(QStringLiteral("minimumSeedHoursEnabled"), minimumSeedHoursEnabled);
  root.insert(QStringLiteral("minimumSeedHours"), minimumSeedHours);
  root.insert(QStringLiteral("minimumRatioEnabled"), minimumRatioEnabled);
  root.insert(QStringLiteral("minimumRatio"), minimumRatio);
  root.insert(QStringLiteral("minimumInactiveHoursEnabled"), minimumInactiveHoursEnabled);
  root.insert(QStringLiteral("minimumInactiveHours"), minimumInactiveHours);
  root.insert(QStringLiteral("maximumRemovalsEnabled"), maximumRemovalsEnabled);
  root.insert(QStringLiteral("maximumRemovalsPerRun"), maximumRemovalsPerRun);
  root.insert(QStringLiteral("cleanupStopFreeEnabled"), cleanupStopFreeEnabled);
  root.insert(QStringLiteral("cleanupStopFreeBytes"), cleanupStopFreeBytes);
  root.insert(QStringLiteral("protectUploadingEnabled"), protectUploadingEnabled);
  root.insert(QStringLiteral("protectUploadBytesPerSecond"), protectUploadBytesPerSecond);
  root.insert(QStringLiteral("protectWhenSpeedUnknown"), protectWhenSpeedUnknown);
  root.insert(QStringLiteral("protectRecentUploadHours"), protectRecentUploadHours);
  root.insert(QStringLiteral("cleanupGraceEnabled"), cleanupGraceEnabled);
  root.insert(QStringLiteral("cleanupGraceHours"), cleanupGraceHours);
  root.insert(QStringLiteral("smartCleanupOrder"), smartCleanupOrder);
  root.insert(QStringLiteral("minimumCopiesEnabled"), minimumCopiesEnabled);
  root.insert(QStringLiteral("minimumCopiesAcrossClients"), minimumCopiesAcrossClients);
  root.insert(QStringLiteral("protectedTags"), QJsonArray::fromStringList(protectedTags));
  root.insert(QStringLiteral("protectedTrackerTerms"), QJsonArray::fromStringList(protectedTrackerTerms));
  root.insert(QStringLiteral("cleanupScheduleEnabled"), cleanupScheduleEnabled);
  root.insert(QStringLiteral("cleanupScheduleStartHour"), cleanupScheduleStartHour);
  root.insert(QStringLiteral("cleanupScheduleEndHour"), cleanupScheduleEndHour);
  root.insert(QStringLiteral("cleanupBatchPercent"), cleanupBatchPercent);

  QJsonArray policies;
  for (const TorrentAutomationClientPolicy& policy : clients) {
    policies.append(QJsonObject{{QStringLiteral("clientId"), policy.clientId},
                                {QStringLiteral("enabled"), policy.enabled},
                                {QStringLiteral("priority"), policy.priority},
                                {QStringLiteral("maxActiveDownloads"), policy.maxActiveDownloads},
                                {QStringLiteral("maxManagedTorrents"), policy.maxManagedTorrents},
                                {QStringLiteral("minimumFreeBytes"), policy.minimumFreeBytes},
                                {QStringLiteral("targetFreePercent"), policy.targetFreePercent},
                                {QStringLiteral("configuredCapacityBytes"), policy.configuredCapacityBytes},
                                {QStringLiteral("maximumDownloadBytesPerSecond"), policy.maximumDownloadBytesPerSecond},
                                {QStringLiteral("requestTimeoutSeconds"), policy.requestTimeoutSeconds},
                                {QStringLiteral("retryAttempts"), policy.retryAttempts},
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
