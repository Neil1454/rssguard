// For license of this file, see <project-root-folder>/LICENSE.md.

#ifndef GUINOTIFICATIONCOORDINATOR_H
#define GUINOTIFICATIONCOORDINATOR_H

#include "miscellaneous/notification.h"
#include "core/message.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>

class Application;
class Feed;
class FeedDownloadResults;
class TrayIcon;
class QTimer;
class QWidget;
struct GuiAction;
struct GuiMessage;
struct GuiMessageDestination;

class GuiNotificationCoordinator : public QObject {
  public:
    explicit GuiNotificationCoordinator(Application* application);
    ~GuiNotificationCoordinator() override;

    TrayIcon* trayIcon();
    void setMainForm();
    void showTrayIcon();
    void deleteTrayIcon();
    void offerChanges();
    void offerPolls() const;
    void showGuiMessage(Notification::Event event,
                        const GuiMessage& message,
                        const GuiMessageDestination& destination,
                        const GuiAction& action,
                        QWidget* parent);
    void showMessagesNumber(int unread_messages, bool any_feed_has_new_unread_messages);
    void onFeedUpdatesStarted();
    void onFeedUpdatesProgress(const Feed* feed, int current, int total);
    void onFeedUpdatesFinished(const FeedDownloadResults& results);

  private:
    enum class ExclusiveState { Disabled, Sleeping, Baselining, Collecting, Sending };
    void updateExclusiveMode();
    void startExclusiveCycle();
    void handleExclusiveFeedResults(const FeedDownloadResults& results);
    void collectExclusiveArticles(const QHash<Feed*, QList<Message>>& articles, bool baseline);
    void beginExclusiveDispatch(const QString& reason);
    void enterExclusiveSleep(const QString& detail);
    void storeExclusiveStatus(const QString& state);
    void showGuiMessageCore(Notification::Event event,
                            const GuiMessage& message,
                            const GuiMessageDestination& destination,
                            const GuiAction& action,
                            QWidget* parent);

  private:
    Application* m_application;
    QPointer<TrayIcon> m_trayIcon;
    QTimer* m_exclusiveTimer = nullptr;
    ExclusiveState m_exclusiveState = ExclusiveState::Disabled;
    QDateTime m_exclusiveCycleStart;
    QDateTime m_exclusiveCutoff;
    QDateTime m_exclusiveMonitorEnd;
    QDateTime m_exclusiveNextPoll;
    QDateTime m_exclusiveNextWake;
    QHash<Feed*, QList<Message>> m_exclusiveArticles;
    QSet<QString> m_exclusiveArticleKeys;
    bool m_exclusiveDispatchStarted = false;
};

#endif // GUINOTIFICATIONCOORDINATOR_H
