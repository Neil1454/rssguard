// For license of this file, see <project-root-folder>/LICENSE.md.

#include "torrent/torrentclient.h"

#include "network-web/basenetworkaccessmanager.h"
#include "network-web/networkfactory.h"

#include <QAuthenticator>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkCookieJar>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrlQuery>

#include <utility>

namespace {
  void setJson(QNetworkRequest& request) {
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  }

  QByteArray responseCookie(QNetworkReply* reply, const QByteArray& name) {
    const QByteArray cookiePrefix = name + QByteArrayLiteral("=");
    for (const auto& header : reply->rawHeaderPairs()) {
      if (header.first.compare("Set-Cookie", Qt::CaseInsensitive) == 0 && header.second.startsWith(cookiePrefix))
        return header.second.left(header.second.indexOf(';'));
    }
    return {};
  }

  QByteArray responseCookieWithPrefix(QNetworkReply* reply, const QByteArray& prefix) {
    for (const auto& header : reply->rawHeaderPairs()) {
      if (header.first.compare("Set-Cookie", Qt::CaseInsensitive) == 0 && header.second.startsWith(prefix))
        return header.second.left(header.second.indexOf(';'));
    }
    return {};
  }

  QString xmlEscape(QString text) {
    return text.replace(QLatin1Char('&'), QStringLiteral("&amp;"))
      .replace(QLatin1Char('<'), QStringLiteral("&lt;"))
      .replace(QLatin1Char('>'), QStringLiteral("&gt;"))
      .replace(QLatin1Char('"'), QStringLiteral("&quot;"))
      .replace(QLatin1Char('\''), QStringLiteral("&apos;"));
  }
}

TorrentClient::TorrentClient(TorrentClientConfig config, QObject* parent)
  : QObject(parent), m_config(std::move(config)), m_network(new BaseNetworkAccessManager(this)) {
  if (!m_config.useRssGuardProxy) m_network->setProxy(QNetworkProxy::NoProxy);
}

TorrentClient::~TorrentClient() = default;

const TorrentClientConfig& TorrentClient::config() const { return m_config; }

void TorrentClient::fetchStatus() {
  connect(this, &TorrentClient::testFinished, this, [this](bool success, const QString& message) {
    TorrentClientStatus status;
    status.reachable = success;
    status.detail = success ? tr("Connection available; workload and disk space are estimated for this adapter.") : message;
    emit statusFinished(status);
  });
  testConnection();
}

void TorrentClient::removeTorrent(const QString& hash, bool deleteData) {
  Q_UNUSED(hash)
  Q_UNUSED(deleteData)
  emit removeFinished(false, tr("Safe remote removal is not supported by this client adapter."));
}

QUrl TorrentClient::endpoint(const QString& path) const {
  QString base = m_config.baseUrl.trimmed();
  while (base.endsWith(QLatin1Char('/'))) base.chop(1);
  if (path.isEmpty()) return QUrl(base);
  return QUrl(base + (path.startsWith(QLatin1Char('/')) ? path : QLatin1Char('/') + path));
}

void TorrentClient::applyBasicAuthentication(QNetworkRequest& request) const {
  if (!m_config.username.isEmpty() || !m_config.password.isEmpty()) {
    request.setRawHeader("Authorization", "Basic " + (m_config.username + QLatin1Char(':') + m_config.password).toUtf8().toBase64());
  }
}

QString TorrentClient::networkFailure(QNetworkReply* reply) const {
  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (status == 401 || status == 403) return tr("Authentication failed (HTTP %1).").arg(status);
  if (reply->error() != QNetworkReply::NoError) return NetworkFactory::networkErrorText(reply->error());
  return tr("Server returned HTTP %1.").arg(status);
}

TorrentClient* TorrentClient::create(const TorrentClientConfig& config, QObject* parent) {
  switch (config.type) {
    case TorrentClientType::QBittorrent: return new QBittorrentClient(config, parent);
    case TorrentClientType::Transmission: return new TransmissionClient(config, parent);
    case TorrentClientType::Flood: return new FloodClient(config, parent);
    case TorrentClientType::RTorrent: return new RTorrentClient(config, parent);
    case TorrentClientType::Deluge: return new DelugeClient(config, parent);
    case TorrentClientType::RQBit: return new RQBitClient(config, parent);
    case TorrentClientType::Porla: return new PorlaClient(config, parent);
  }
  return nullptr;
}

QBittorrentClient::QBittorrentClient(const TorrentClientConfig& config, QObject* parent) : TorrentClient(config, parent) {}

void QBittorrentClient::authenticate(const std::function<void(bool, const QString&)>& continuation) {
  QNetworkRequest request(endpoint(QStringLiteral("/api/v2/auth/login")));
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
  const QByteArray origin = endpoint(QString()).toString(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment).toUtf8();
  request.setRawHeader("Origin", origin);
  request.setRawHeader("Referer", origin + '/');
  const QByteArray form = "username=" + QUrl::toPercentEncoding(m_config.username) +
                          "&password=" + QUrl::toPercentEncoding(m_config.password);
  QNetworkReply* reply = m_network->post(request, form);
  connect(reply, &QNetworkReply::finished, this, [this, reply, continuation]() {
    const QByteArray body = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool oldApiSuccess = status == 200 && body.trimmed() == "Ok.";
    const bool newApiSuccess = status == 204;
    const bool ok = reply->error() == QNetworkReply::NoError && (oldApiSuccess || newApiSuccess);
    if (ok) {
      m_cookie = responseCookie(reply, "SID");
      if (m_cookie.isEmpty()) m_cookie = responseCookieWithPrefix(reply, "QBT_SID_");
    }
    const QString error = ok ? QString() : (reply->error() == QNetworkReply::NoError ? tr("Authentication failed.") : networkFailure(reply));
    reply->deleteLater();
    continuation(ok, error);
  });
}

void QBittorrentClient::testConnection() {
  authenticate([this](bool ok, const QString& error) {
    if (!ok) { emit testFinished(false, error); return; }
    QNetworkRequest request(endpoint(QStringLiteral("/api/v2/app/version")));
    request.setRawHeader("Referer", endpoint(QString()).toString(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment).toUtf8() + '/');
    if (!m_cookie.isEmpty()) request.setRawHeader("Cookie", m_cookie);
    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
      const QString version = QString::fromUtf8(reply->readAll()).trimmed();
      const bool ok = reply->error() == QNetworkReply::NoError && !version.isEmpty();
      emit testFinished(ok, ok ? tr("Connected successfully to qBittorrent %1.").arg(version) : networkFailure(reply));
      reply->deleteLater();
    });
  });
}

void QBittorrentClient::addTorrents(const QStringList& urls) {
  authenticate([this, urls](bool ok, const QString& error) {
    if (!ok) { emit addFinished(0, urls.size(), error); return; }
    auto* multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    auto addField = [multipart](const QByteArray& name, const QString& value) {
      if (value.isEmpty()) return;
      QHttpPart part;
      part.setHeader(QNetworkRequest::ContentDispositionHeader, QStringLiteral("form-data; name=\"%1\"").arg(QString::fromLatin1(name)));
      part.setBody(value.toUtf8());
      multipart->append(part);
    };
    addField("urls", urls.join(QLatin1Char('\n')));
    addField("savepath", m_config.savePath);
    addField("category", m_config.category);
    addField("tags", m_config.tags.join(QLatin1Char(',')));
    QNetworkRequest request(endpoint(QStringLiteral("/api/v2/torrents/add")));
    request.setRawHeader("Referer", endpoint(QString()).toString(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment).toUtf8() + '/');
    if (!m_cookie.isEmpty()) request.setRawHeader("Cookie", m_cookie);
    QNetworkReply* reply = m_network->post(request, multipart);
    multipart->setParent(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, count = urls.size()]() {
      const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
      const QString successMessage = status == 202 ? tr("qBittorrent queued %1 torrent(s).").arg(count)
                                                   : tr("Sent %1 torrent(s) to qBittorrent.").arg(count);
      emit addFinished(ok ? count : 0, ok ? 0 : count, ok ? successMessage : networkFailure(reply));
      reply->deleteLater();
    });
  });
}

void QBittorrentClient::fetchStatus() {
  authenticate([this](bool ok, const QString& error) {
    if (!ok) {
      TorrentClientStatus status;
      status.detail = error;
      emit statusFinished(status);
      return;
    }
    QNetworkRequest request(endpoint(QStringLiteral("/api/v2/sync/maindata")));
    if (!m_cookie.isEmpty()) request.setRawHeader("Cookie", m_cookie);
    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
      TorrentClientStatus status;
      const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
      status.reachable = reply->error() == QNetworkReply::NoError && !response.isEmpty();
      if (!status.reachable) {
        status.detail = networkFailure(reply);
      }
      else {
        const QJsonObject server = response.value(QStringLiteral("server_state")).toObject();
        if (server.contains(QStringLiteral("free_space_on_disk"))) {
          status.freeBytes = server.value(QStringLiteral("free_space_on_disk")).toVariant().toLongLong();
          status.liveSpace = status.freeBytes >= 0;
        }
        const QJsonObject torrents = response.value(QStringLiteral("torrents")).toObject();
        for (auto it = torrents.constBegin(); it != torrents.constEnd(); ++it) {
          const QJsonObject object = it.value().toObject();
          TorrentRemoteItem item;
          item.hash = it.key();
          item.name = object.value(QStringLiteral("name")).toString();
          item.sizeBytes = object.value(QStringLiteral("size")).toVariant().toLongLong();
          item.progress = object.value(QStringLiteral("progress")).toDouble();
          item.ratio = object.value(QStringLiteral("ratio")).toDouble();
          item.added = QDateTime::fromSecsSinceEpoch(object.value(QStringLiteral("added_on")).toVariant().toLongLong());
          item.completed = QDateTime::fromSecsSinceEpoch(object.value(QStringLiteral("completion_on")).toVariant().toLongLong());
          item.lastActivity = QDateTime::fromSecsSinceEpoch(object.value(QStringLiteral("last_activity")).toVariant().toLongLong());
          const QString state = object.value(QStringLiteral("state")).toString();
          item.managedByAutomation = object.value(QStringLiteral("tags")).toString()
                                               .split(QLatin1Char(','), Qt::SkipEmptyParts)
                                               .contains(QStringLiteral("rssguard-auto"));
          item.downloading = state.contains(QStringLiteral("downloading"), Qt::CaseInsensitive) ||
                             state == QStringLiteral("metaDL") || state == QStringLiteral("stalledDL");
          item.seeding = state.contains(QStringLiteral("upload"), Qt::CaseInsensitive) || state == QStringLiteral("stalledUP");
          status.activeDownloads += item.downloading ? 1 : 0;
          status.queuedDownloads += state.contains(QStringLiteral("queued"), Qt::CaseInsensitive) ? 1 : 0;
          status.seeding += item.seeding ? 1 : 0;
          status.torrents.append(item);
        }
        status.detail = tr("Live qBittorrent status");
      }
      reply->deleteLater();
      emit statusFinished(status);
    });
  });
}

void QBittorrentClient::removeTorrent(const QString& hash, bool deleteData) {
  authenticate([this, hash, deleteData](bool ok, const QString& error) {
    if (!ok) { emit removeFinished(false, error); return; }
    QNetworkRequest request(endpoint(QStringLiteral("/api/v2/torrents/delete")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    if (!m_cookie.isEmpty()) request.setRawHeader("Cookie", m_cookie);
    const QByteArray data = "hashes=" + QUrl::toPercentEncoding(hash) +
                            "&deleteFiles=" + QByteArray(deleteData ? "true" : "false");
    QNetworkReply* reply = m_network->post(request, data);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
      const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      const bool ok = reply->error() == QNetworkReply::NoError && code >= 200 && code < 300;
      emit removeFinished(ok, ok ? tr("Torrent removed from qBittorrent.") : networkFailure(reply));
      reply->deleteLater();
    });
  });
}

TransmissionClient::TransmissionClient(const TorrentClientConfig& config, QObject* parent) : TorrentClient(config, parent) {
  connect(m_network,
          &QNetworkAccessManager::authenticationRequired,
          this,
          [this](QNetworkReply*, QAuthenticator* authenticator) {
            authenticator->setUser(m_config.username);
            authenticator->setPassword(m_config.password);
          });
}

void TransmissionClient::rpc(const QJsonObject& object, const std::function<void(QNetworkReply*, const QJsonObject&)>& callback, bool retry) {
  QNetworkRequest request(endpoint(QString()));
  setJson(request);
  applyBasicAuthentication(request);
  if (!m_sessionId.isEmpty()) request.setRawHeader("X-Transmission-Session-Id", m_sessionId.toUtf8());
  const QByteArray body = QJsonDocument(object).toJson(QJsonDocument::Compact);
  QNetworkReply* reply = m_network->post(request, body);
  connect(reply, &QNetworkReply::finished, this, [this, reply, object, callback, retry]() {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 409 && retry) {
      m_sessionId = QString::fromUtf8(reply->rawHeader("X-Transmission-Session-Id"));
      reply->deleteLater();
      rpc(object, callback, false);
      return;
    }
    const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
    callback(reply, response);
    reply->deleteLater();
  });
}

void TransmissionClient::testConnection() {
  rpc(QJsonObject{{QStringLiteral("method"), QStringLiteral("session-get")}}, [this](QNetworkReply* reply, const QJsonObject& response) {
    const bool ok = reply->error() == QNetworkReply::NoError && response.value(QStringLiteral("result")).toString() == QStringLiteral("success");
    const QJsonObject arguments = response.value(QStringLiteral("arguments")).toObject();
    const QString version = arguments.value(QStringLiteral("version")).toString();
    m_rpcVersion = arguments.value(QStringLiteral("rpc-version")).toInt();
    emit testFinished(ok,
                      ok ? tr("Connected successfully to Transmission %1 (RPC %2).")
                             .arg(version, QString::number(m_rpcVersion))
                         : networkFailure(reply));
  });
}

void TransmissionClient::addTorrents(const QStringList& urls) {
  rpc(QJsonObject{{QStringLiteral("method"), QStringLiteral("session-get")}},
      [this, urls](QNetworkReply* reply, const QJsonObject& response) {
        const bool ok = reply->error() == QNetworkReply::NoError &&
                        response.value(QStringLiteral("result")).toString() == QStringLiteral("success");
        if (!ok) {
          emit addFinished(0, urls.size(), networkFailure(reply));
          return;
        }
        m_rpcVersion = response.value(QStringLiteral("arguments"))
                         .toObject()
                         .value(QStringLiteral("rpc-version"))
                         .toInt();
        m_pending.clear();
        for (const QString& url : urls) m_pending.enqueue(url);
        m_added = m_failed = 0;
        addNext();
      });
}

void TransmissionClient::addNext() {
  if (m_pending.isEmpty()) {
    emit addFinished(m_added, m_failed, tr("Transmission accepted %1 torrent(s); %2 failed.").arg(m_added).arg(m_failed));
    return;
  }
  QJsonObject arguments{{QStringLiteral("filename"), m_pending.dequeue()}};
  if (!m_config.savePath.isEmpty()) arguments.insert(QStringLiteral("download-dir"), m_config.savePath);
  // torrent-add labels were introduced by Transmission RPC 17 (Transmission 4.0).
  if (m_rpcVersion >= 17 && !m_config.tags.isEmpty())
    arguments.insert(QStringLiteral("labels"), QJsonArray::fromStringList(m_config.tags));
  rpc(QJsonObject{{QStringLiteral("method"), QStringLiteral("torrent-add")}, {QStringLiteral("arguments"), arguments}},
      [this](QNetworkReply* reply, const QJsonObject& response) {
        if (reply->error() == QNetworkReply::NoError && response.value(QStringLiteral("result")).toString() == QStringLiteral("success")) ++m_added;
        else ++m_failed;
        addNext();
      });
}

void TransmissionClient::fetchStatus() {
  const QJsonArray fields{QStringLiteral("hashString"), QStringLiteral("name"), QStringLiteral("totalSize"),
                          QStringLiteral("percentDone"), QStringLiteral("uploadRatio"), QStringLiteral("addedDate"),
                          QStringLiteral("doneDate"), QStringLiteral("activityDate"), QStringLiteral("status"),
                          QStringLiteral("labels")};
  rpc(QJsonObject{{QStringLiteral("method"), QStringLiteral("torrent-get")},
                  {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("fields"), fields}}}},
      [this](QNetworkReply* reply, const QJsonObject& response) {
        TorrentClientStatus status;
        status.reachable = reply->error() == QNetworkReply::NoError &&
                           response.value(QStringLiteral("result")).toString() == QStringLiteral("success");
        if (!status.reachable) {
          status.detail = networkFailure(reply);
          emit statusFinished(status);
          return;
        }
        for (const QJsonValue& value : response.value(QStringLiteral("arguments")).toObject()
                                                .value(QStringLiteral("torrents")).toArray()) {
          const QJsonObject object = value.toObject();
          TorrentRemoteItem item;
          item.hash = object.value(QStringLiteral("hashString")).toString();
          item.name = object.value(QStringLiteral("name")).toString();
          item.sizeBytes = object.value(QStringLiteral("totalSize")).toVariant().toLongLong();
          item.progress = object.value(QStringLiteral("percentDone")).toDouble();
          item.ratio = object.value(QStringLiteral("uploadRatio")).toDouble();
          item.added = QDateTime::fromSecsSinceEpoch(object.value(QStringLiteral("addedDate")).toVariant().toLongLong());
          item.completed = QDateTime::fromSecsSinceEpoch(object.value(QStringLiteral("doneDate")).toVariant().toLongLong());
          item.lastActivity = QDateTime::fromSecsSinceEpoch(object.value(QStringLiteral("activityDate")).toVariant().toLongLong());
          const int state = object.value(QStringLiteral("status")).toInt();
          for (const QJsonValue& label : object.value(QStringLiteral("labels")).toArray())
            if (label.toString() == QStringLiteral("rssguard-auto")) item.managedByAutomation = true;
          item.downloading = state == 4;
          item.seeding = state == 6;
          status.activeDownloads += item.downloading ? 1 : 0;
          status.queuedDownloads += state == 3 ? 1 : 0;
          status.seeding += item.seeding ? 1 : 0;
          status.torrents.append(item);
        }
        const QString path = m_config.savePath.isEmpty() ? QStringLiteral(".") : m_config.savePath;
        rpc(QJsonObject{{QStringLiteral("method"), QStringLiteral("free-space")},
                        {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("path"), path}}}},
            [this, status](QNetworkReply* spaceReply, const QJsonObject& spaceResponse) mutable {
              if (spaceReply->error() == QNetworkReply::NoError &&
                  spaceResponse.value(QStringLiteral("result")).toString() == QStringLiteral("success")) {
                const QJsonObject args = spaceResponse.value(QStringLiteral("arguments")).toObject();
                status.freeBytes = args.value(QStringLiteral("size-bytes")).toVariant().toLongLong();
                status.totalBytes = args.value(QStringLiteral("total_size")).toVariant().toLongLong();
                if (status.totalBytes <= 0) status.totalBytes = args.value(QStringLiteral("total-size")).toVariant().toLongLong();
                status.liveSpace = status.freeBytes >= 0;
              }
              status.detail = tr("Live Transmission status");
              emit statusFinished(status);
            });
      });
}

void TransmissionClient::removeTorrent(const QString& hash, bool deleteData) {
  const QJsonObject arguments{{QStringLiteral("ids"), QJsonArray{hash}},
                              {QStringLiteral("delete-local-data"), deleteData}};
  rpc(QJsonObject{{QStringLiteral("method"), QStringLiteral("torrent-remove")},
                  {QStringLiteral("arguments"), arguments}},
      [this](QNetworkReply* reply, const QJsonObject& response) {
        const bool ok = reply->error() == QNetworkReply::NoError &&
                        response.value(QStringLiteral("result")).toString() == QStringLiteral("success");
        emit removeFinished(ok, ok ? tr("Torrent removed from Transmission.") : networkFailure(reply));
      });
}

FloodClient::FloodClient(const TorrentClientConfig& config, QObject* parent) : TorrentClient(config, parent) {}

void FloodClient::authenticate(const std::function<void(bool, const QString&)>& continuation) {
  if (!m_config.token.isEmpty()) {
    m_cookie = "jwt=" + m_config.token.toUtf8();
    continuation(true, QString());
    return;
  }
  QNetworkRequest request(endpoint(QStringLiteral("/api/auth/authenticate")));
  setJson(request);
  const QJsonObject credentials{{QStringLiteral("username"), m_config.username}, {QStringLiteral("password"), m_config.password}};
  QNetworkReply* reply = m_network->post(request, QJsonDocument(credentials).toJson(QJsonDocument::Compact));
  connect(reply, &QNetworkReply::finished, this, [this, reply, continuation]() {
    const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
    const bool ok = reply->error() == QNetworkReply::NoError && response.value(QStringLiteral("success")).toBool();
    if (ok) m_cookie = responseCookie(reply, "jwt");
    const QString error = ok ? QString() : networkFailure(reply);
    reply->deleteLater();
    continuation(ok, error);
  });
}

void FloodClient::testConnection() {
  authenticate([this](bool ok, const QString& error) {
    if (!ok) { emit testFinished(false, error); return; }
    QNetworkRequest request(endpoint(QStringLiteral("/api/auth/verify")));
    if (!m_cookie.isEmpty()) request.setRawHeader("Cookie", m_cookie);
    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
      const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
      const bool ok = reply->error() == QNetworkReply::NoError && response.contains(QStringLiteral("configs"));
      emit testFinished(ok, ok ? tr("Connected successfully to Flood.") : networkFailure(reply));
      reply->deleteLater();
    });
  });
}

void FloodClient::addTorrents(const QStringList& urls) {
  authenticate([this, urls](bool ok, const QString& error) {
    if (!ok) { emit addFinished(0, urls.size(), error); return; }
    QNetworkRequest request(endpoint(QStringLiteral("/api/torrents/add-urls")));
    if (!m_cookie.isEmpty()) request.setRawHeader("Cookie", m_cookie);
    setJson(request);
    QJsonObject payload{{QStringLiteral("urls"), QJsonArray::fromStringList(urls)}, {QStringLiteral("start"), true}};
    if (!m_config.savePath.isEmpty()) payload.insert(QStringLiteral("destination"), m_config.savePath);
    if (!m_config.tags.isEmpty()) payload.insert(QStringLiteral("tags"), QJsonArray::fromStringList(m_config.tags));
    QNetworkReply* reply = m_network->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, count = urls.size()]() {
      const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      const QByteArray body = reply->readAll();
      const bool responseOk = reply->error() == QNetworkReply::NoError;
      int accepted = 0;
      QString message;
      if (responseOk && (status == 200 || status == 202)) {
        accepted = count;
        message = status == 202 ? tr("Flood queued %1 torrent(s).").arg(count)
                                : tr("Flood accepted %1 torrent(s).").arg(count);
      }
      else if (responseOk && status == 207) {
        accepted = qMin(count, static_cast<int>(QJsonDocument::fromJson(body).array().size()));
        message = tr("Flood accepted %1 of %2 torrent(s).").arg(accepted).arg(count);
      }
      else if (status == 500) {
        message = tr("Flood reported an internal server error, but it may still have submitted the torrent. Check Flood before trying again.");
      }
      else {
        message = networkFailure(reply);
      }
      emit addFinished(accepted, count - accepted, message);
      reply->deleteLater();
    });
  });
}

RTorrentClient::RTorrentClient(const TorrentClientConfig& config, QObject* parent) : TorrentClient(config, parent) {
  connect(m_network,
          &QNetworkAccessManager::authenticationRequired,
          this,
          [this](QNetworkReply*, QAuthenticator* authenticator) {
            authenticator->setUser(m_config.username);
            authenticator->setPassword(m_config.password);
          });
}

QByteArray RTorrentClient::methodCall(const QString& method, const QStringList& values) const {
  QString xml = QStringLiteral("<?xml version=\"1.0\"?><methodCall><methodName>%1</methodName><params>").arg(xmlEscape(method));
  for (const QString& value : values) xml += QStringLiteral("<param><value><string>%1</string></value></param>").arg(xmlEscape(value));
  xml += QStringLiteral("</params></methodCall>");
  return xml.toUtf8();
}

void RTorrentClient::call(const QString& method, const QStringList& values, const std::function<void(QNetworkReply*, const QByteArray&)>& callback) {
  QNetworkRequest request(endpoint(QString()));
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/xml"));
  applyBasicAuthentication(request);
  QNetworkReply* reply = m_network->post(request, methodCall(method, values));
  connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
    const QByteArray body = reply->readAll();
    callback(reply, body);
    reply->deleteLater();
  });
}

void RTorrentClient::testConnection() {
  call(QStringLiteral("system.client_version"), {}, [this](QNetworkReply* reply, const QByteArray& body) {
    static const QRegularExpression valueExpression(QStringLiteral("<string>([^<]+)</string>"));
    const QString version = valueExpression.match(QString::fromUtf8(body)).captured(1);
    const bool ok = reply->error() == QNetworkReply::NoError && !body.contains("<fault>") && !version.isEmpty();
    QString message;
    if (ok) {
      message = tr("Connected successfully to rTorrent %1.").arg(version);
    }
    else if (reply->error() == QNetworkReply::ContentOperationNotPermittedError) {
      message = tr("This URL does not accept XML-RPC requests. For ruTorrent, use its XML-RPC endpoint, usually the ruTorrent address followed by /plugins/httprpc/action.php. Some installations expose /RPC2 instead.");
    }
    else {
      message = networkFailure(reply);
    }
    emit testFinished(ok, message);
  });
}

void RTorrentClient::addTorrents(const QStringList& urls) {
  m_pending.clear();
  for (const QString& url : urls) m_pending.enqueue(url);
  m_added = m_failed = 0;
  addNext();
}

void RTorrentClient::addNext() {
  if (m_pending.isEmpty()) {
    emit addFinished(m_added, m_failed, tr("rTorrent accepted %1 torrent(s); %2 failed.").arg(m_added).arg(m_failed));
    return;
  }
  QStringList arguments{QString(), m_pending.dequeue()};
  if (!m_config.savePath.isEmpty()) arguments.append(QStringLiteral("d.directory.set=%1").arg(m_config.savePath));
  if (!m_config.category.isEmpty()) arguments.append(QStringLiteral("d.custom1.set=%1").arg(m_config.category));
  call(QStringLiteral("load.start"), arguments, [this](QNetworkReply* reply, const QByteArray& body) {
    if (reply->error() == QNetworkReply::NoError && !body.contains("<fault>")) ++m_added;
    else ++m_failed;
    addNext();
  });
}

DelugeClient::DelugeClient(const TorrentClientConfig& config, QObject* parent) : TorrentClient(config, parent) {}

void DelugeClient::rpc(const QString& method,
                       const QJsonArray& params,
                       const std::function<void(QNetworkReply*, const QJsonObject&)>& callback) {
  QNetworkRequest request(endpoint(QStringLiteral("/json")));
  setJson(request);
  if (!m_cookie.isEmpty()) request.setRawHeader("Cookie", m_cookie);
  const QJsonObject payload{{QStringLiteral("method"), method},
                            {QStringLiteral("params"), params},
                            {QStringLiteral("id"), ++m_requestId}};
  QNetworkReply* reply = m_network->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
  connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
    const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
    callback(reply, response);
    reply->deleteLater();
  });
}

void DelugeClient::authenticate(const std::function<void(bool, const QString&)>& continuation) {
  rpc(QStringLiteral("auth.login"), QJsonArray{m_config.password}, [this, continuation](QNetworkReply* reply,
                                                                                       const QJsonObject& response) {
    const bool noRpcError = !response.contains(QStringLiteral("error")) || response.value(QStringLiteral("error")).isNull();
    const bool ok = reply->error() == QNetworkReply::NoError && noRpcError &&
                    response.value(QStringLiteral("result")).toBool();
    if (ok) m_cookie = responseCookie(reply, "_session_id");
    const QString error = ok ? QString()
                             : (reply->error() == QNetworkReply::NoError
                                  ? tr("Deluge Web password was rejected.")
                                  : networkFailure(reply));
    continuation(ok, error);
  });
}

void DelugeClient::prepare(const std::function<void(bool, const QString&)>& continuation) {
  authenticate([this, continuation](bool ok, const QString& error) {
    if (!ok) {
      continuation(false, error);
      return;
    }
    rpc(QStringLiteral("web.connected"), {}, [this, continuation](QNetworkReply* reply, const QJsonObject& response) {
      if (reply->error() != QNetworkReply::NoError || !response.value(QStringLiteral("error")).isNull()) {
        continuation(false, networkFailure(reply));
        return;
      }
      if (response.value(QStringLiteral("result")).toBool()) {
        continuation(true, QString());
        return;
      }
      rpc(QStringLiteral("web.get_hosts"), {}, [this, continuation](QNetworkReply* hostsReply,
                                                                    const QJsonObject& hostsResponse) {
        const QJsonArray hosts = hostsResponse.value(QStringLiteral("result")).toArray();
        if (hostsReply->error() != QNetworkReply::NoError || hosts.isEmpty() || hosts.first().toArray().isEmpty()) {
          continuation(false, tr("Deluge Web is logged in, but no Deluge daemon is configured."));
          return;
        }
        const QString hostId = hosts.first().toArray().first().toString();
        rpc(QStringLiteral("web.connect"), QJsonArray{hostId}, [this, continuation](QNetworkReply* connectReply,
                                                                                   const QJsonObject& connectResponse) {
          const bool connected = connectReply->error() == QNetworkReply::NoError &&
                                 connectResponse.value(QStringLiteral("error")).isNull() &&
                                 connectResponse.value(QStringLiteral("result")).isArray();
          continuation(connected, connected ? QString() : tr("Deluge Web could not connect to its configured daemon."));
        });
      });
    });
  });
}

void DelugeClient::testConnection() {
  prepare([this](bool ok, const QString& error) {
    if (!ok) {
      emit testFinished(false, error);
      return;
    }
    rpc(QStringLiteral("web.get_hosts"), {}, [this](QNetworkReply* reply, const QJsonObject& response) {
      const QJsonArray hosts = response.value(QStringLiteral("result")).toArray();
      if (reply->error() != QNetworkReply::NoError || hosts.isEmpty() || hosts.first().toArray().isEmpty()) {
        emit testFinished(false, tr("Connected to Deluge Web, but no daemon information was returned."));
        return;
      }
      const QString hostId = hosts.first().toArray().first().toString();
      rpc(QStringLiteral("web.get_host_status"), QJsonArray{hostId}, [this](QNetworkReply* statusReply,
                                                                           const QJsonObject& statusResponse) {
        const QJsonArray status = statusResponse.value(QStringLiteral("result")).toArray();
        const bool success = statusReply->error() == QNetworkReply::NoError && status.size() >= 3;
        emit testFinished(success,
                          success ? tr("Connected successfully to Deluge %1 (%2).")
                                      .arg(status.at(2).toString(), status.at(1).toString())
                                  : networkFailure(statusReply));
      });
    });
  });
}

void DelugeClient::addTorrents(const QStringList& urls) {
  prepare([this, urls](bool ok, const QString& error) {
    if (!ok) {
      emit addFinished(0, urls.size(), error);
      return;
    }
    m_pending.clear();
    for (const QString& url : urls) m_pending.enqueue(url);
    m_added = m_failed = 0;
    addNext();
  });
}

void DelugeClient::addNext() {
  if (m_pending.isEmpty()) {
    emit addFinished(m_added, m_failed, tr("Deluge accepted %1 torrent(s); %2 failed.").arg(m_added).arg(m_failed));
    return;
  }

  const QString url = m_pending.dequeue();
  QJsonObject options;
  if (!m_config.savePath.isEmpty()) options.insert(QStringLiteral("download_location"), m_config.savePath);
  const bool magnet = url.startsWith(QStringLiteral("magnet:"), Qt::CaseInsensitive);
  rpc(magnet ? QStringLiteral("core.add_torrent_magnet") : QStringLiteral("core.add_torrent_url"),
      QJsonArray{url, options},
      [this](QNetworkReply* reply, const QJsonObject& response) {
        const bool ok = reply->error() == QNetworkReply::NoError && response.value(QStringLiteral("error")).isNull() &&
                        !response.value(QStringLiteral("result")).isNull();
        if (ok) ++m_added;
        else ++m_failed;
        addNext();
      });
}

void DelugeClient::fetchStatus() {
  prepare([this](bool ok, const QString& error) {
    if (!ok) {
      TorrentClientStatus status;
      status.detail = error;
      emit statusFinished(status);
      return;
    }
    const QJsonArray keys{QStringLiteral("name"), QStringLiteral("total_size"), QStringLiteral("progress"),
                          QStringLiteral("ratio"), QStringLiteral("time_added"), QStringLiteral("completed_time"),
                          QStringLiteral("last_seen_complete"), QStringLiteral("state")};
    rpc(QStringLiteral("core.get_torrents_status"), QJsonArray{QJsonObject(), keys},
        [this](QNetworkReply* reply, const QJsonObject& response) {
          TorrentClientStatus status;
          status.reachable = reply->error() == QNetworkReply::NoError &&
                             (!response.contains(QStringLiteral("error")) || response.value(QStringLiteral("error")).isNull());
          if (!status.reachable) {
            status.detail = networkFailure(reply);
            emit statusFinished(status);
            return;
          }
          const QJsonObject torrents = response.value(QStringLiteral("result")).toObject();
          for (auto it = torrents.constBegin(); it != torrents.constEnd(); ++it) {
            const QJsonObject object = it.value().toObject();
            TorrentRemoteItem item;
            item.hash = it.key();
            item.name = object.value(QStringLiteral("name")).toString();
            item.sizeBytes = object.value(QStringLiteral("total_size")).toVariant().toLongLong();
            item.progress = object.value(QStringLiteral("progress")).toDouble() / 100.0;
            item.ratio = object.value(QStringLiteral("ratio")).toDouble();
            item.added = QDateTime::fromSecsSinceEpoch(object.value(QStringLiteral("time_added")).toVariant().toLongLong());
            item.completed = QDateTime::fromSecsSinceEpoch(object.value(QStringLiteral("completed_time")).toVariant().toLongLong());
            item.lastActivity = QDateTime::fromSecsSinceEpoch(object.value(QStringLiteral("last_seen_complete")).toVariant().toLongLong());
            const QString state = object.value(QStringLiteral("state")).toString();
            item.downloading = state.compare(QStringLiteral("Downloading"), Qt::CaseInsensitive) == 0;
            item.seeding = state.compare(QStringLiteral("Seeding"), Qt::CaseInsensitive) == 0;
            status.activeDownloads += item.downloading ? 1 : 0;
            status.queuedDownloads += state.contains(QStringLiteral("Queued"), Qt::CaseInsensitive) ? 1 : 0;
            status.seeding += item.seeding ? 1 : 0;
            status.torrents.append(item);
          }
          rpc(QStringLiteral("core.get_free_space"), QJsonArray{m_config.savePath},
              [this, status](QNetworkReply* spaceReply, const QJsonObject& spaceResponse) mutable {
                if (spaceReply->error() == QNetworkReply::NoError &&
                    (!spaceResponse.contains(QStringLiteral("error")) || spaceResponse.value(QStringLiteral("error")).isNull())) {
                  status.freeBytes = spaceResponse.value(QStringLiteral("result")).toVariant().toLongLong();
                  status.liveSpace = status.freeBytes >= 0;
                }
                status.detail = tr("Live Deluge status");
                emit statusFinished(status);
              });
        });
  });
}

void DelugeClient::removeTorrent(const QString& hash, bool deleteData) {
  prepare([this, hash, deleteData](bool ok, const QString& error) {
    if (!ok) { emit removeFinished(false, error); return; }
    rpc(QStringLiteral("core.remove_torrent"), QJsonArray{hash, deleteData},
        [this](QNetworkReply* reply, const QJsonObject& response) {
          const bool ok = reply->error() == QNetworkReply::NoError && response.value(QStringLiteral("error")).isNull() &&
                          response.value(QStringLiteral("result")).toBool();
          emit removeFinished(ok, ok ? tr("Torrent removed from Deluge.") : networkFailure(reply));
        });
  });
}

RQBitClient::RQBitClient(const TorrentClientConfig& config, QObject* parent) : TorrentClient(config, parent) {
  connect(m_network,
          &QNetworkAccessManager::authenticationRequired,
          this,
          [this](QNetworkReply*, QAuthenticator* authenticator) {
            authenticator->setUser(m_config.username);
            authenticator->setPassword(m_config.password);
          });
}

void RQBitClient::testConnection() {
  QNetworkRequest request(endpoint(QString()));
  applyBasicAuthentication(request);
  QNetworkReply* reply = m_network->get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
    const bool ok = reply->error() == QNetworkReply::NoError &&
                    response.value(QStringLiteral("server")).toString().compare(QStringLiteral("rqbit"), Qt::CaseInsensitive) == 0;
    const QString version = response.value(QStringLiteral("version")).toString();
    emit testFinished(ok,
                      ok ? (version.isEmpty() ? tr("Connected successfully to rQBit.")
                                              : tr("Connected successfully to rQBit %1.").arg(version))
                         : networkFailure(reply));
    reply->deleteLater();
  });
}

void RQBitClient::addTorrents(const QStringList& urls) {
  m_pending.clear();
  for (const QString& url : urls) m_pending.enqueue(url);
  m_added = m_failed = 0;
  addNext();
}

void RQBitClient::addNext() {
  if (m_pending.isEmpty()) {
    emit addFinished(m_added, m_failed, tr("rQBit accepted %1 torrent(s); %2 failed.").arg(m_added).arg(m_failed));
    return;
  }
  QUrl url = endpoint(QStringLiteral("/torrents"));
  if (!m_config.savePath.isEmpty()) {
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("output_folder"), m_config.savePath);
    url.setQuery(query);
  }
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/plain; charset=utf-8"));
  applyBasicAuthentication(request);
  QNetworkReply* reply = m_network->post(request, m_pending.dequeue().toUtf8());
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
    ok ? ++m_added : ++m_failed;
    reply->deleteLater();
    addNext();
  });
}

PorlaClient::PorlaClient(const TorrentClientConfig& config, QObject* parent) : TorrentClient(config, parent) {}

void PorlaClient::rpc(const QString& method,
                      const QJsonObject& params,
                      const std::function<void(QNetworkReply*, const QJsonObject&)>& callback) {
  QNetworkRequest request(endpoint(QStringLiteral("/api/v1/jsonrpc")));
  setJson(request);
  if (!m_config.token.trimmed().isEmpty())
    request.setRawHeader("Authorization", "Bearer " + m_config.token.trimmed().toUtf8());
  const QJsonObject payload{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                            {QStringLiteral("method"), method},
                            {QStringLiteral("params"), params},
                            {QStringLiteral("id"), ++m_requestId}};
  QNetworkReply* reply = m_network->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
  connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
    const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
    callback(reply, response);
    reply->deleteLater();
  });
}

void PorlaClient::testConnection() {
  rpc(QStringLiteral("sys.versions"), {}, [this](QNetworkReply* reply, const QJsonObject& response) {
    const QJsonValue result = response.value(QStringLiteral("result"));
    const bool noRpcError = !response.contains(QStringLiteral("error")) || response.value(QStringLiteral("error")).isNull();
    const bool ok = reply->error() == QNetworkReply::NoError && noRpcError &&
                    !result.isUndefined() && !result.isNull();
    QString version;
    if (result.isObject()) {
      const QJsonObject versions = result.toObject();
      version = versions.value(QStringLiteral("porla")).toString();
      if (version.isEmpty()) version = versions.value(QStringLiteral("version")).toString();
    }
    emit testFinished(ok,
                      ok ? (version.isEmpty() ? tr("Connected successfully to Porla.")
                                              : tr("Connected successfully to Porla %1.").arg(version))
                         : networkFailure(reply));
  });
}

void PorlaClient::addTorrents(const QStringList& urls) {
  m_pending.clear();
  for (const QString& url : urls) m_pending.enqueue(url);
  m_added = m_failed = 0;
  addNext();
}

void PorlaClient::addNext() {
  if (m_pending.isEmpty()) {
    emit addFinished(m_added, m_failed, tr("Porla accepted %1 torrent(s); %2 failed.").arg(m_added).arg(m_failed));
    return;
  }
  const QString source = m_pending.dequeue();
  if (source.startsWith(QStringLiteral("magnet:"), Qt::CaseInsensitive)) {
    submitTorrent(source);
    return;
  }
  QNetworkReply* reply = m_network->get(QNetworkRequest(QUrl(source)));
  connect(reply, &QNetworkReply::finished, this, [this, reply, source]() {
    const QByteArray data = reply->readAll();
    const bool ok = reply->error() == QNetworkReply::NoError && !data.isEmpty() && data.size() <= 20 * 1024 * 1024;
    reply->deleteLater();
    if (ok) submitTorrent(source, data);
    else {
      ++m_failed;
      addNext();
    }
  });
}

void PorlaClient::submitTorrent(const QString& source, const QByteArray& torrentData) {
  QJsonObject params;
  if (torrentData.isEmpty()) params.insert(QStringLiteral("magnet_uri"), source);
  else params.insert(QStringLiteral("ti"), QString::fromLatin1(torrentData.toBase64()));
  if (!m_config.savePath.isEmpty()) params.insert(QStringLiteral("save_path"), m_config.savePath);
  if (!m_config.category.isEmpty()) params.insert(QStringLiteral("preset"), m_config.category);
  rpc(QStringLiteral("torrents.add"), params, [this](QNetworkReply* reply, const QJsonObject& response) {
    const QJsonValue result = response.value(QStringLiteral("result"));
    const bool noRpcError = !response.contains(QStringLiteral("error")) || response.value(QStringLiteral("error")).isNull();
    const bool ok = reply->error() == QNetworkReply::NoError && noRpcError &&
                    !result.isUndefined() && !result.isNull();
    ok ? ++m_added : ++m_failed;
    addNext();
  });
}

void PorlaClient::fetchStatus() {
  rpc(QStringLiteral("torrents.list"), QJsonObject{{QStringLiteral("page"), 0}, {QStringLiteral("page_size"), 10000}},
      [this](QNetworkReply* reply, const QJsonObject& response) {
        TorrentClientStatus status;
        status.reachable = reply->error() == QNetworkReply::NoError &&
                           (!response.contains(QStringLiteral("error")) || response.value(QStringLiteral("error")).isNull());
        if (!status.reachable) {
          status.detail = networkFailure(reply);
          emit statusFinished(status);
          return;
        }
        const QJsonArray torrents = response.value(QStringLiteral("result")).toObject()
                                            .value(QStringLiteral("torrents")).toArray();
        for (const QJsonValue& value : torrents) {
          const QJsonObject object = value.toObject();
          TorrentRemoteItem item;
          const QJsonValue hash = object.value(QStringLiteral("info_hash"));
          item.hash = hash.isString() ? hash.toString() : QString::fromUtf8(QJsonDocument(hash.toArray()).toJson(QJsonDocument::Compact));
          item.name = object.value(QStringLiteral("name")).toString();
          item.sizeBytes = object.value(QStringLiteral("size")).toVariant().toLongLong();
          item.progress = object.value(QStringLiteral("progress")).toDouble();
          item.ratio = object.value(QStringLiteral("ratio")).toDouble();
          item.downloading = item.progress < 1.0 && object.value(QStringLiteral("download_rate")).toDouble() > 0;
          item.seeding = item.progress >= 1.0;
          status.activeDownloads += item.downloading ? 1 : 0;
          status.seeding += item.seeding ? 1 : 0;
          status.torrents.append(item);
        }
        const QString path = m_config.savePath.isEmpty() ? QStringLiteral(".") : m_config.savePath;
        rpc(QStringLiteral("fs.space"), QJsonObject{{QStringLiteral("path"), path}},
            [this, status](QNetworkReply* spaceReply, const QJsonObject& spaceResponse) mutable {
              const QJsonObject space = spaceResponse.value(QStringLiteral("result")).toObject();
              if (spaceReply->error() == QNetworkReply::NoError &&
                  (!spaceResponse.contains(QStringLiteral("error")) || spaceResponse.value(QStringLiteral("error")).isNull())) {
                status.freeBytes = space.value(QStringLiteral("available")).toVariant().toLongLong();
                status.totalBytes = space.value(QStringLiteral("capacity")).toVariant().toLongLong();
                status.liveSpace = status.freeBytes >= 0;
              }
              status.detail = tr("Live Porla status");
              emit statusFinished(status);
            });
      });
}

void PorlaClient::removeTorrent(const QString& hash, bool deleteData) {
  QJsonValue hashValue = hash;
  const QJsonDocument parsed = QJsonDocument::fromJson(hash.toUtf8());
  if (parsed.isArray()) hashValue = parsed.array();
  rpc(QStringLiteral("torrents.remove"),
      QJsonObject{{QStringLiteral("info_hashes"), QJsonArray{hashValue}}, {QStringLiteral("remove_data"), deleteData}},
      [this](QNetworkReply* reply, const QJsonObject& response) {
        const bool ok = reply->error() == QNetworkReply::NoError &&
                        (!response.contains(QStringLiteral("error")) || response.value(QStringLiteral("error")).isNull());
        emit removeFinished(ok, ok ? tr("Torrent removed from Porla.") : networkFailure(reply));
      });
}
