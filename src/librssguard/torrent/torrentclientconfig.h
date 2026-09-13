// For license of this file, see <project-root-folder>/LICENSE.md.

#ifndef TORRENTCLIENTCONFIG_H
#define TORRENTCLIENTCONFIG_H

#include "definitions/definitions.h"

#include <QList>
#include <QDateTime>
#include <QString>

class Settings;

enum class TorrentClientType {
  QBittorrent = 0,
  Transmission = 1,
  Flood = 2,
  RTorrent = 3,
  Deluge = 4,
  RQBit = 5,
  Porla = 6
};

struct RSSGUARD_DLLSPEC TorrentClientConfig {
  QString id;
  QString name;
  TorrentClientType type = TorrentClientType::QBittorrent;
  QString baseUrl;
  QString username;
  QString password;
  QString token;
  QString buttonColor;
  bool colorNotificationButtons = true;
  bool colorSettingsLists = true;
  bool enabled = true;
  int priority = 0;
  bool useRssGuardProxy = true;
  bool isDefault = false;
  QString savePath;
  QString category;
  QStringList tags;

  // Results of the latest non-destructive capability test.
  bool capabilityTested = false;
  bool capabilityConnected = false;
  bool capabilityLiveStatus = false;
  bool capabilityFreeSpace = false;
  bool capabilityTorrentList = false;
  bool capabilityRemoval = false;
  QDateTime capabilityTestedAt;
  QString capabilityDetail;

  bool isValid(QString* error = nullptr) const;
  static QString typeName(TorrentClientType type);
  static QList<TorrentClientConfig> enabledInPriorityOrder(const QList<TorrentClientConfig>& clients);
  static QList<TorrentClientConfig> load(Settings* settings);
  static void save(Settings* settings, const QList<TorrentClientConfig>& clients);
};

#endif // TORRENTCLIENTCONFIG_H
