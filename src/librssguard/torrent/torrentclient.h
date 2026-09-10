// For license of this file, see <project-root-folder>/LICENSE.md.

#ifndef TORRENTCLIENT_H
#define TORRENTCLIENT_H

#include "torrent/torrentclientconfig.h"

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QQueue>

#include <functional>

class BaseNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QUrl;
class QJsonArray;

class RSSGUARD_DLLSPEC TorrentClient : public QObject {
    Q_OBJECT

  public:
    explicit TorrentClient(TorrentClientConfig config, QObject* parent = nullptr);
    virtual ~TorrentClient();

    const TorrentClientConfig& config() const;
    virtual void testConnection() = 0;
    virtual void addTorrents(const QStringList& urls) = 0;

    static TorrentClient* create(const TorrentClientConfig& config, QObject* parent = nullptr);

  signals:
    void testFinished(bool success, const QString& message);
    void addFinished(int added, int failed, const QString& message);

  protected:
    QUrl endpoint(const QString& path) const;
    void applyBasicAuthentication(QNetworkRequest& request) const;
    QString networkFailure(QNetworkReply* reply) const;

    TorrentClientConfig m_config;
    BaseNetworkAccessManager* m_network;
};

class QBittorrentClient final : public TorrentClient {
    Q_OBJECT
  public:
    explicit QBittorrentClient(const TorrentClientConfig& config, QObject* parent = nullptr);
    void testConnection() override;
    void addTorrents(const QStringList& urls) override;

  private:
    void authenticate(const std::function<void(bool, const QString&)>& continuation);
    QByteArray m_cookie;
};

class TransmissionClient final : public TorrentClient {
    Q_OBJECT
  public:
    explicit TransmissionClient(const TorrentClientConfig& config, QObject* parent = nullptr);
    void testConnection() override;
    void addTorrents(const QStringList& urls) override;

  private:
    void rpc(const QJsonObject& request, const std::function<void(QNetworkReply*, const QJsonObject&)>& callback, bool retry = true);
    void addNext();
    QString m_sessionId;
    int m_rpcVersion = 0;
    QQueue<QString> m_pending;
    int m_added = 0;
    int m_failed = 0;
};

class FloodClient final : public TorrentClient {
    Q_OBJECT
  public:
    explicit FloodClient(const TorrentClientConfig& config, QObject* parent = nullptr);
    void testConnection() override;
    void addTorrents(const QStringList& urls) override;

  private:
    void authenticate(const std::function<void(bool, const QString&)>& continuation);
    QByteArray m_cookie;
};

class RTorrentClient final : public TorrentClient {
    Q_OBJECT
  public:
    explicit RTorrentClient(const TorrentClientConfig& config, QObject* parent = nullptr);
    void testConnection() override;
    void addTorrents(const QStringList& urls) override;

  private:
    QByteArray methodCall(const QString& method, const QStringList& values = {}) const;
    void call(const QString& method, const QStringList& values, const std::function<void(QNetworkReply*, const QByteArray&)>& callback);
    void addNext();
    QQueue<QString> m_pending;
    int m_added = 0;
    int m_failed = 0;
};

class DelugeClient final : public TorrentClient {
    Q_OBJECT
  public:
    explicit DelugeClient(const TorrentClientConfig& config, QObject* parent = nullptr);
    void testConnection() override;
    void addTorrents(const QStringList& urls) override;

  private:
    void rpc(const QString& method,
             const QJsonArray& params,
             const std::function<void(QNetworkReply*, const QJsonObject&)>& callback);
    void authenticate(const std::function<void(bool, const QString&)>& continuation);
    void prepare(const std::function<void(bool, const QString&)>& continuation);
    void addNext();

    QByteArray m_cookie;
    QQueue<QString> m_pending;
    int m_requestId = 0;
    int m_added = 0;
    int m_failed = 0;
};

class RQBitClient final : public TorrentClient {
    Q_OBJECT
  public:
    explicit RQBitClient(const TorrentClientConfig& config, QObject* parent = nullptr);
    void testConnection() override;
    void addTorrents(const QStringList& urls) override;

  private:
    void addNext();
    QQueue<QString> m_pending;
    int m_added = 0;
    int m_failed = 0;
};

class PorlaClient final : public TorrentClient {
    Q_OBJECT
  public:
    explicit PorlaClient(const TorrentClientConfig& config, QObject* parent = nullptr);
    void testConnection() override;
    void addTorrents(const QStringList& urls) override;

  private:
    void rpc(const QString& method,
             const QJsonObject& params,
             const std::function<void(QNetworkReply*, const QJsonObject&)>& callback);
    void addNext();
    void submitTorrent(const QString& source, const QByteArray& torrentData = {});

    QQueue<QString> m_pending;
    int m_requestId = 0;
    int m_added = 0;
    int m_failed = 0;
};

#endif // TORRENTCLIENT_H
