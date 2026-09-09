// For license of this file, see <project-root-folder>/LICENSE.md.

#ifndef ARTICLELISTNOTIFICATION_H
#define ARTICLELISTNOTIFICATION_H

#include "core/feedsmodel.h"
#include "core/message.h"
#include "gui/notifications/basetoastnotification.h"

#include "ui_articlelistnotification.h"

class Feed;
class ArticleListNotificationModel;
class QHBoxLayout;
struct TorrentClientConfig;

class ArticleListNotification : public BaseToastNotification {
    Q_OBJECT

  public:
    explicit ArticleListNotification(QWidget* parent = nullptr);

    void loadResults(const QHash<Feed*, QList<Message>>& new_messages);

  signals:
    void openingArticleInArticleListRequested(Feed* feed, const Message& msg);
    void dataChangeNotificationTriggered(RootItem* item, FeedsModel::ExternalDataChange change);
    void oneArticleSetReadUnreadById(int article_id, RootItem::ReadStatus read);

  public:
    virtual bool eventFilter(QObject* watched, QEvent* event);

  private slots:
    void openArticleInArticleList();
    void openArticleInWebBrowser();
    void onMessageSelected(const QModelIndex& current, const QModelIndex& previous);
    void showFeed(int index);
    void markAllRead();
    void rebuildTorrentActions();

  private:
    void markAsRead(Feed* feed, const QList<Message>& articles);
    void sendSelectedToTorrentClient(const TorrentClientConfig& config);

    Feed* selectedFeed(int index = -1) const;
    Message& selectedMessage();
    QList<Message> selectedMessages() const;

  private:
    Ui::ArticleListNotification m_ui;
    ArticleListNotificationModel* m_model;
    QHash<Feed*, QList<Message>> m_newMessages;
    QHBoxLayout* m_torrentActionsLayout = nullptr;
};

#endif // ARTICLELISTNOTIFICATION_H
