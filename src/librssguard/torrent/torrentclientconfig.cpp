// For license of this file, see <project-root-folder>/LICENSE.md.

#include "torrent/torrentclientconfig.h"

#include "miscellaneous/settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QUrl>

namespace {
  const QString ConfigGroup = QStringLiteral("TorrentClients");
  const QString ConfigKey = QStringLiteral("clients");
  const QString SecretsGroup = QStringLiteral("TorrentClientSecrets");
}

bool TorrentClientConfig::isValid(QString* error) const {
  if (name.trimmed().isEmpty()) {
    if (error != nullptr) *error = QObject::tr("Client name cannot be empty.");
    return false;
  }

  const QUrl url(baseUrl.trimmed());
  if (!url.isValid() || url.host().isEmpty() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))) {
    if (error != nullptr) *error = QObject::tr("Server URL must be a valid HTTP or HTTPS URL.");
    return false;
  }

  return true;
}

QString TorrentClientConfig::typeName(TorrentClientType type) {
  switch (type) {
    case TorrentClientType::QBittorrent: return QStringLiteral("qBittorrent");
    case TorrentClientType::Transmission: return QStringLiteral("Transmission");
    case TorrentClientType::Flood: return QStringLiteral("Flood");
    case TorrentClientType::RTorrent: return QStringLiteral("rTorrent / ruTorrent");
    case TorrentClientType::Deluge: return QStringLiteral("Deluge");
  }
  return QStringLiteral("Unknown");
}

QList<TorrentClientConfig> TorrentClientConfig::load(Settings* settings) {
  QList<TorrentClientConfig> clients;
  const QByteArray data = settings->value(ConfigGroup, ConfigKey).toByteArray();
  const QJsonArray array = QJsonDocument::fromJson(data).array();

  for (const QJsonValue& value : array) {
    const QJsonObject object = value.toObject();
    TorrentClientConfig client;
    client.id = object.value(QStringLiteral("id")).toString();
    client.name = object.value(QStringLiteral("name")).toString();
    client.type = static_cast<TorrentClientType>(object.value(QStringLiteral("type")).toInt());
    client.baseUrl = object.value(QStringLiteral("baseUrl")).toString();
    client.username = object.value(QStringLiteral("username")).toString();
    client.useRssGuardProxy = object.value(QStringLiteral("useRssGuardProxy")).toBool(true);
    client.isDefault = object.value(QStringLiteral("isDefault")).toBool(false);
    client.savePath = object.value(QStringLiteral("savePath")).toString();
    client.category = object.value(QStringLiteral("category")).toString();
    for (const QJsonValue& tag : object.value(QStringLiteral("tags")).toArray()) client.tags.append(tag.toString());
    client.password = settings->password(SecretsGroup, client.id + QStringLiteral("/password")).toString();
    client.token = settings->password(SecretsGroup, client.id + QStringLiteral("/token")).toString();
    if (!client.id.isEmpty()) clients.append(client);
  }
  return clients;
}

void TorrentClientConfig::save(Settings* settings, const QList<TorrentClientConfig>& clients) {
  settings->remove(ConfigGroup, ConfigKey);
  settings->remove(SecretsGroup);
  QJsonArray array;
  bool defaultSeen = false;

  for (TorrentClientConfig client : clients) {
    if (client.id.isEmpty()) client.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (client.isDefault && defaultSeen) client.isDefault = false;
    defaultSeen = defaultSeen || client.isDefault;

    QJsonObject object;
    object.insert(QStringLiteral("id"), client.id);
    object.insert(QStringLiteral("name"), client.name.trimmed());
    object.insert(QStringLiteral("type"), static_cast<int>(client.type));
    object.insert(QStringLiteral("baseUrl"), client.baseUrl.trimmed());
    object.insert(QStringLiteral("username"), client.username);
    object.insert(QStringLiteral("useRssGuardProxy"), client.useRssGuardProxy);
    object.insert(QStringLiteral("isDefault"), client.isDefault);
    object.insert(QStringLiteral("savePath"), client.savePath);
    object.insert(QStringLiteral("category"), client.category);
    object.insert(QStringLiteral("tags"), QJsonArray::fromStringList(client.tags));
    array.append(object);

    settings->setPassword(SecretsGroup, client.id + QStringLiteral("/password"), client.password);
    settings->setPassword(SecretsGroup, client.id + QStringLiteral("/token"), client.token);
  }

  settings->setValue(ConfigGroup, ConfigKey, QJsonDocument(array).toJson(QJsonDocument::Compact));
}
