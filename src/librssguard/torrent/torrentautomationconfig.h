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
  int weight = 100;
  int maxActiveDownloads = 3;
  int maxManagedTorrents = 0;
  qint64 minimumFreeBytes = 20LL * 1024 * 1024 * 1024;
  qint64 configuredCapacityBytes = 0;
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
  TorrentRoutingStrategy strategy = TorrentRoutingStrategy::Balanced;
  int retryMinutes = 15;
  int historyLimit = 500;
  int roundRobinCursor = 0;

  bool cleanupEnabled = false;
  bool deleteData = false;
  bool cleanupRequireConfirmation = true;
  int minimumSeedHours = 168;
  double minimumRatio = 1.0;
  int minimumInactiveHours = 24;
  int maximumRemovalsPerRun = 1;
  qint64 cleanupStopFreeBytes = 40LL * 1024 * 1024 * 1024;

  QList<TorrentAutomationClientPolicy> clients;
  QList<TorrentAutomationRule> rules;

  static TorrentAutomationConfig load(Settings* settings);
  void save(Settings* settings) const;
  TorrentAutomationClientPolicy policyFor(const QString& clientId) const;
  static QString strategyName(TorrentRoutingStrategy strategy);
};

#endif // TORRENTAUTOMATIONCONFIG_H
