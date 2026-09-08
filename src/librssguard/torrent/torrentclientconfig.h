// For license of this file, see <project-root-folder>/LICENSE.md.

#ifndef TORRENTCLIENTCONFIG_H
#define TORRENTCLIENTCONFIG_H

#include "definitions/definitions.h"

#include <QList>
#include <QString>

class Settings;

enum class TorrentClientType {
  QBittorrent = 0,
  Transmission = 1,
  Flood = 2,
  RTorrent = 3
};

struct RSSGUARD_DLLSPEC TorrentClientConfig {
  QString id;
  QString name;
  TorrentClientType type = TorrentClientType::QBittorrent;
  QString baseUrl;
  QString username;
  QString password;
  QString token;
  bool useRssGuardProxy = true;
  bool isDefault = false;
  QString savePath;
  QString category;
  QStringList tags;

  bool isValid(QString* error = nullptr) const;
  static QString typeName(TorrentClientType type);
  static QList<TorrentClientConfig> load(Settings* settings);
  static void save(Settings* settings, const QList<TorrentClientConfig>& clients);
};

#endif // TORRENTCLIENTCONFIG_H
