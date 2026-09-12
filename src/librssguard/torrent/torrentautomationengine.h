// For license of this file, see <project-root-folder>/LICENSE.md.

#ifndef TORRENTAUTOMATIONENGINE_H
#define TORRENTAUTOMATIONENGINE_H

#include "torrent/torrentautomationconfig.h"
#include "torrent/torrentclient.h"
#include "torrent/torrentclientconfig.h"

#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QQueue>

class Feed;
class Message;

class RSSGUARD_DLLSPEC TorrentAutomationEngine final : public QObject {
    Q_OBJECT

  public:
    static TorrentAutomationEngine* instance(QObject* parent = nullptr);
    static void processNewArticles(const QHash<Feed*, QList<Message>>& articles, QObject* parent = nullptr);

    bool busy() const;
    QStringList recentActivity() const;

  signals:
    void activityAdded(const QString& text);
    void busyChanged(bool busy);

  private:
    struct Job {
      QString key;
      QString title;
      QString url;
      QString feedId;
      QStringList allowedClientIds;
      qint64 sizeBytes = 0;
      int attempt = 0;
    };

    explicit TorrentAutomationEngine(QObject* parent = nullptr);
    void enqueue(const QHash<Feed*, QList<Message>>& articles);
    bool ruleMatches(const TorrentAutomationRule& rule, const QString& feedId, const Message& message) const;
    void beginBatch();
    void queryNextClient();
    void processNextJob();
    QList<int> eligibleClientIndexes(const Job& job) const;
    int selectClient(const QList<int>& eligible);
    void sendJob(const Job& job, int clientIndex);
    bool tryCleanup(const Job& job);
    void finishBatch();
    void record(const QString& state, const Job& job, const QString& clientId, const QString& detail);
    bool wasProcessed(const QString& key) const;
    void markProcessed(const QString& key);
    qint64 estimatedManagedBytes(const QString& clientId) const;
    QString jobKey(const QString& url) const;
    void loadRuntime();
    void saveRuntime();
    void notify(const QString& title, const QString& detail, bool warning = false);

    TorrentAutomationConfig m_config;
    QList<TorrentClientConfig> m_clients;
    QList<TorrentClientStatus> m_statuses;
    QQueue<Job> m_jobs;
    QStringList m_processed;
    QJsonArray m_history;
    QJsonArray m_managed;
    int m_queryIndex = 0;
    int m_cleanupCount = 0;
    bool m_busy = false;
};

#endif // TORRENTAUTOMATIONENGINE_H
