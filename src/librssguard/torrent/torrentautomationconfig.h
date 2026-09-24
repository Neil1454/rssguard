// For license of this file, see <project-root-folder>/LICENSE.md.

#ifndef TORRENTAUTOMATIONCONFIG_H
#define TORRENTAUTOMATIONCONFIG_H

#include "definitions/definitions.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

class Settings;

enum class TorrentRoutingStrategy {
  Priority = 0,
  LeastBusy = 1,
  MostFreeSpace = 2,
  RoundRobin = 3,
  Weighted = 4,
  Balanced = 5
};

struct RSSGUARD_DLLSPEC TorrentAutomationClientPolicy {
  QString clientId;
  bool enabled = true;
  int priority = 1;
  int maxActiveDownloads = 3;
  int maxManagedTorrents = 0;
  qint64 minimumFreeBytes = 20LL * 1024 * 1024 * 1024;
  double targetFreePercent = 0.0;
  qint64 configuredCapacityBytes = 0;
  qint64 maximumDownloadBytesPerSecond = 0;
  int requestTimeoutSeconds = 0;
  int retryAttempts = -1;
  bool allowCleanup = false;
};

struct RSSGUARD_DLLSPEC TorrentAutomationRule {
  QString id;
  QString name;
  bool enabled = true;
  QStringList feedIds;
  QString requiredText;
  QString excludedText;
  QString titleRegularExpression;
  qint64 minimumSizeBytes = 0;
  qint64 maximumSizeBytes = 0;
  QStringList clientIds;
};

struct RSSGUARD_DLLSPEC TorrentAutomationConfig {
  bool enabled = false;
  bool dryRun = true;
  bool showNotifications = true;
  bool paused = false;
  bool silentNotifications = false;
  bool ignoreInitialFeedBatch = false;
  bool markInitialFeedBatchRead = false;
  bool exclusiveModeEnabled = false;
  bool exclusiveModeArmed = false;
  int exclusiveSleepMinutes = 60;
  int exclusiveFreshnessMinutes = 3;
  int exclusiveMonitoringMinutes = 15;
  int exclusivePollMinutes = 1;
  int exclusiveBatchSize = 5;
  bool exclusiveSendPartialBatch = true;
  int notificationDurationSeconds = 0;
  int maximumConsecutiveAssignments = 1;
  QString speedDisplayUnit = QStringLiteral("MiB/s");
  TorrentRoutingStrategy strategy = TorrentRoutingStrategy::Balanced;
  int retryMinutes = 1;
  bool retryEnabled = true;
  int retryAttempts = 3;
  int retryInitialSeconds = 60;
  int retryMaximumSeconds = 900;
  bool retryExponentialBackoff = true;
  int requestTimeoutSeconds = 15;
  int historyLimit = 500;
  QString storageDisplayUnit = QStringLiteral("GiB");
  int roundRobinCursor = 0;
  qint64 unknownTorrentSizeBytes = 10LL * 1024 * 1024 * 1024;
  bool reconciliationEnabled = true;
  bool reserveRemainingBytes = true;
  bool preventDuplicateAcrossClients = true;
  int reconciliationMinutes = 30;
  bool circuitBreakerEnabled = true;
  int circuitBreakerFailures = 3;
  int circuitBreakerCooldownMinutes = 15;
  int circuitBreakerRecoverySuccesses = 2;
  bool scheduleEnabled = false;
  int scheduleStartHour = 0;
  int scheduleEndHour = 24;

  bool cleanupEnabled = false;
  bool deleteData = false;
  bool cleanupRequireConfirmation = true;
  int cleanupConfirmationSeconds = 60;
  bool cleanupIncludeUnmanaged = false;
  QStringList protectedTorrentHashes;
  bool maximumRetentionEnabled = false;
  int maximumRetentionHours = 720;
  bool maximumRetentionStrict = true;
  bool minimumSeedHoursEnabled = true;
  int minimumSeedHours = 168;
  bool minimumRatioEnabled = true;
  double minimumRatio = 1.0;
  bool minimumInactiveHoursEnabled = true;
  int minimumInactiveHours = 24;
  bool maximumRemovalsEnabled = true;
  int maximumRemovalsPerRun = 1;
  bool cleanupStopFreeEnabled = true;
  qint64 cleanupStopFreeBytes = 40LL * 1024 * 1024 * 1024;
  bool protectUploadingEnabled = true;
  qint64 protectUploadBytesPerSecond = 256LL * 1024;
  bool protectWhenSpeedUnknown = true;
  int protectRecentUploadHours = 24;
  bool cleanupGraceEnabled = true;
  int cleanupGraceHours = 24;
  bool smartCleanupOrder = true;
  bool minimumCopiesEnabled = false;
  int minimumCopiesAcrossClients = 1;
  QStringList protectedTags;
  QStringList protectedTrackerTerms;
  bool cleanupScheduleEnabled = false;
  int cleanupScheduleStartHour = 0;
  int cleanupScheduleEndHour = 24;
  double cleanupBatchPercent = 5.0;

  QList<TorrentAutomationClientPolicy> clients;
  QList<TorrentAutomationRule> rules;

  static TorrentAutomationConfig load(Settings* settings);
  void save(Settings* settings) const;
  TorrentAutomationClientPolicy policyFor(const QString& clientId) const;
  static QString strategyName(TorrentRoutingStrategy strategy);
};

#endif // TORRENTAUTOMATIONCONFIG_H
