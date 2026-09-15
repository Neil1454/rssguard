// For license of this file, see <project-root-folder>/LICENSE.md.

#ifndef TORRENTAUTOMATIONENGINE_H
#define TORRENTAUTOMATIONENGINE_H

#include "torrent/torrentautomationconfig.h"
#include "torrent/torrentclient.h"
#include "torrent/torrentclientconfig.h"
#include "core/message.h"

#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QPointer>
#include <QQueue>

class Feed;
class QWidget;

class RSSGUARD_DLLSPEC TorrentAutomationEngine final : public QObject {
    Q_OBJECT

  public:
    static TorrentAutomationEngine* instance(QObject* parent = nullptr);
    static void processNewArticles(const QHash<Feed*, QList<Message>>& articles, QObject* parent = nullptr);
    static void processApprovedArticles(Feed* feed,
                                        const QList<Message>& articles,
                                        QWidget* dialogParent = nullptr,
                                        QObject* parent = nullptr);
    static void processDirectArticles(const TorrentClientConfig& client,
                                      const QList<Message>& articles,
                                      QObject* parent = nullptr);

    bool busy() const;
    QStringList recentActivity() const;
    QStringList pendingRetries() const;
    void runDryTest();
    void retryPending(int index);
    void sendPendingToClient(int index, const QString& clientId);
    void cancelPending(int index);

  signals:
    void activityAdded(const QString& text);
    void busyChanged(bool busy);

  private:
    struct Job {
      QString key;
      QString title;
      QString url;
      QString feedId;
      QString ruleName;
      int messageId = 0;
      QStringList allowedClientIds;
      qint64 sizeBytes = 0;
      int attempt = 0;
      QStringList attemptedClientIds;
      QString verificationClientId;
      QDateTime nextAttempt;
      QString queueReason;
      bool manualApproval = false;
      bool directOverride = false;
    };

    explicit TorrentAutomationEngine(QObject* parent = nullptr);
    void enqueue(const QHash<Feed*, QList<Message>>& articles,
                 bool forceDryRun = false,
                 bool manualApproval = false,
                 bool forceEnabled = false);
    bool ruleMatches(const TorrentAutomationRule& rule, const QString& feedId, const Message& message) const;
    void beginBatch();
    void queryClientStatus(int index, int attempt);
    void processNextJob();
    QList<int> eligibleClientIndexes(const Job& job) const;
    int selectClient(const QList<int>& eligible);
    int confirmManualDestination(const Job& job, const QList<int>& eligible, int recommended);
    QString clientRestriction(int index, const Job& job, bool* softRestriction = nullptr) const;
    qint64 clientFreeSpaceTarget(int index) const;
    void sendJob(const Job& job, int clientIndex);
    bool tryCleanup(const Job& job);
    void finishBatch();
    void record(const QString& state, const Job& job, const QString& clientId, const QString& detail);
    bool wasProcessed(const QString& key) const;
    void markProcessed(const QString& key);
    qint64 estimatedManagedBytes(const QString& clientId) const;
    qint64 outstandingManagedBytes(const QString& clientId) const;
    void reconcileManagedState();
    bool withinHourWindow(int startHour, int endHour) const;
    void scheduleForWindow(Job job, int startHour, const QString& reason);
    void scheduleAt(Job job, const QDateTime& when, const QString& reason);
    int completedCopyCount(const QString& hash) const;
    QDateTime lastUploadActivity(const QString& clientId, const QString& hash) const;
    double cleanupScore(const TorrentRemoteItem& item, const QDateTime& now) const;
    bool circuitBreakerOpen(const QString& clientId, QString* detail = nullptr) const;
    bool updateCircuitBreaker(const QString& clientId, bool success, const QString& detail);
    QString jobKey(const QString& url) const;
    void loadRuntime();
    void saveRuntime();
    void notify(const QString& title, const QString& detail, bool warning = false);
    void scheduleRetry(Job job, const QString& reason);
    void armDeferredJob(const Job& job);
    bool isTransientFailure(const QString& message) const;
    bool isAmbiguousFailure(const QString& message) const;

    TorrentAutomationConfig m_config;
    QList<TorrentClientConfig> m_clients;
    QList<TorrentClientStatus> m_statuses;
    QQueue<Job> m_jobs;
    QStringList m_processed;
    QJsonArray m_history;
    QJsonArray m_managed;
    QJsonArray m_cleanupCandidates;
    QJsonArray m_uploadActivity;
    QJsonArray m_clientHealth;
    int m_pendingStatusQueries = 0;
    int m_cleanupCount = 0;
    bool m_busy = false;
    bool m_forcedDryRun = false;
    QList<Job> m_deferredJobs;
    QPointer<QWidget> m_manualDialogParent;
    QHash<Feed*, QList<Message>> m_lastArticles;
    QDateTime m_lastReconcile;
};

#endif // TORRENTAUTOMATIONENGINE_H
