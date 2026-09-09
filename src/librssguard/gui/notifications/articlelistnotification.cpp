// For license of this file, see <project-root-folder>/LICENSE.md.

#include "gui/notifications/articlelistnotification.h"

#include "core/articlelistnotificationmodel.h"
#include "database/databasefactory.h"
#include "database/databasequeries.h"
#include "miscellaneous/iconfactory.h"
#include "miscellaneous/localization.h"
#include "miscellaneous/settings.h"
#include "network-web/webfactory.h"
#include "torrent/torrentclient.h"
#include "torrent/torrentclientconfig.h"
#include "torrent/torrentextractor.h"

#include <QCheckBox>
#include <QAbstractItemView>
#include <QGridLayout>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSizePolicy>
#include <QTreeView>
#include <QWheelEvent>

ArticleListNotification::ArticleListNotification(QWidget* parent)
  : BaseToastNotification(parent), m_model(new ArticleListNotificationModel(this)) {
  m_ui.setupUi(this);

  setupHeading(m_ui.m_lblTitle);
  setupCloseButton(m_ui.m_btnClose);

  m_model->setFont(font());
  m_ui.m_treeArticles->installEventFilter(this);
  m_ui.m_treeArticles->viewport()->installEventFilter(this);

  m_ui.m_btnNextPage->setIcon(qApp->icons()->fromTheme(QSL("arrow-right"), QSL("stock_right")));
  m_ui.m_btnPreviousPage->setIcon(qApp->icons()->fromTheme(QSL("arrow-left"), QSL("stock_left")));
  m_ui.m_btnOpenArticleList->setIcon(qApp->icons()->fromTheme(QSL("view-list-details")));
  m_ui.m_btnOpenWebBrowser->setIcon(qApp->icons()->fromTheme(QSL("document-open")));
  m_ui.m_btnMarkAllRead->setIcon(qApp->icons()->fromTheme(QSL("mail-mark-read")));

  m_ui.m_treeArticles->setModel(m_model);
  m_ui.m_treeArticles->setSelectionBehavior(QAbstractItemView::SelectionBehavior::SelectRows);
  m_ui.m_treeArticles->setSelectionMode(QAbstractItemView::SelectionMode::ExtendedSelection);

  auto* torrentActions = new QWidget(this);
  m_torrentActionsLayout = new QGridLayout(torrentActions);
  m_torrentActionsLayout->setContentsMargins(0, 0, 0, 0);
  m_torrentActionsLayout->setHorizontalSpacing(6);
  m_torrentActionsLayout->setVerticalSpacing(4);
  m_torrentActionsLayout->setColumnStretch(0, 1);
  m_torrentActionsLayout->setColumnStretch(1, 1);
  m_ui.formLayout->insertRow(2, torrentActions);

  connect(m_model,
          &ArticleListNotificationModel::nextPagePossibleChanged,
          m_ui.m_btnNextPage,
          &PlainToolButton::setEnabled);
  connect(m_model,
          &ArticleListNotificationModel::previousPagePossibleChanged,
          m_ui.m_btnPreviousPage,
          &PlainToolButton::setEnabled);
  connect(m_ui.m_btnNextPage, &PlainToolButton::clicked, m_model, &ArticleListNotificationModel::nextPage);
  connect(m_ui.m_btnPreviousPage, &PlainToolButton::clicked, m_model, &ArticleListNotificationModel::previousPage);
  connect(m_ui.m_btnMarkAllRead, &PlainToolButton::clicked, this, &ArticleListNotification::markAllRead);
  connect(m_ui.m_treeArticles,
          &QAbstractItemView::doubleClicked,
          this,
          &ArticleListNotification::openArticleInWebBrowser);
  connect(m_ui.m_btnOpenWebBrowser, &PlainToolButton::clicked, this, &ArticleListNotification::openArticleInWebBrowser);
  connect(m_ui.m_btnOpenArticleList,
          &PlainToolButton::clicked,
          this,
          &ArticleListNotification::openArticleInArticleList);
  connect(m_ui.m_treeArticles->selectionModel(),
          &QItemSelectionModel::currentRowChanged,
          this,
          &ArticleListNotification::onMessageSelected);
  connect(m_ui.m_treeArticles->selectionModel(),
          &QItemSelectionModel::selectionChanged,
          this,
          [this]() { rebuildTorrentActions(); });
  connect(m_model, &QAbstractItemModel::modelReset, this, [this]() {
    if (m_model->rowCount({}) > 0) {
      const QModelIndex first = m_model->index(0, 0);
      m_ui.m_treeArticles->selectionModel()->select(first,
                                                    QItemSelectionModel::SelectionFlag::ClearAndSelect |
                                                      QItemSelectionModel::SelectionFlag::Rows);
      m_ui.m_treeArticles->setCurrentIndex(first);
    }
  });

  setAttribute(Qt::WidgetAttribute::WA_ShowWithoutActivating, true);
  m_ui.m_treeArticles->setAttribute(Qt::WidgetAttribute::WA_NoSystemBackground, true);

  // Make background transparent.
  auto pal = m_ui.m_treeArticles->palette();
  pal.setColor(QPalette::ColorRole::Base, Qt::transparent);
  m_ui.m_treeArticles->setPalette(pal);

  connect(m_ui.m_cmbFeeds,
          QOverload<int>::of(&QComboBox::currentIndexChanged),
          this,
          &ArticleListNotification::showFeed);
}

void ArticleListNotification::loadResults(const QHash<Feed*, QList<Message>>& new_messages) {
  qDebugNN << LOGSEC_NOTIFICATIONS << "Setting up RESULTS notification.";
  setupTimedClosing(false);

  m_newMessages = new_messages;

  m_ui.m_cmbFeeds->model()->sort(0, Qt::SortOrder::AscendingOrder);
  m_ui.m_cmbFeeds->clear();

  auto ks = new_messages.keys();

  std::sort(ks.begin(), ks.end(), [](Feed* lhs, Feed* rhs) {
    return QString::compare(lhs->sanitizedTitle(), rhs->sanitizedTitle(), Qt::CaseSensitivity::CaseInsensitive) < 0;
  });

  for (Feed* fd : std::as_const(ks)) {
    if (fd->isQuiet()) {
      continue;
    }

    if (m_newMessages[fd].size() > 0) {
      m_ui.m_cmbFeeds->addItem(fd->fullIcon(), fd->sanitizedTitle(), QVariant::fromValue(fd));
    }
  }

  m_ui.m_lblTitle->setText(tr("%n feeds fetched", nullptr, m_ui.m_cmbFeeds->count()));
  m_ui.m_lblTitle->setToolTip(m_ui.m_lblTitle->text());
  rebuildTorrentActions();
}

void ArticleListNotification::openArticleInArticleList() {
  emit openingArticleInArticleListRequested(m_ui.m_cmbFeeds->currentData().value<Feed*>(), selectedMessage());

  if (m_newMessages.size() == 1 && m_newMessages.value(m_newMessages.keys().at(0)).size() == 1) {
    // We only have 1 message in 1 feed.
    emit closeRequested(this, false);
  }
}

void ArticleListNotification::onMessageSelected(const QModelIndex& current, const QModelIndex& previous) {
  Q_UNUSED(current)
  Q_UNUSED(previous)

  m_ui.m_btnOpenArticleList->setEnabled(m_ui.m_treeArticles->currentIndex().isValid());

  try {
    Message msg = selectedMessage();

    m_ui.m_btnOpenWebBrowser->setEnabled(!msg.m_url.isEmpty());
  }
  catch (...) {
    m_ui.m_btnOpenWebBrowser->setEnabled(false);
  }

  rebuildTorrentActions();
}

bool ArticleListNotification::staysOpenUntilDismissed() const {
  return qApp->settings()->value(GROUP(GUI), SETTING(GUI::KeepArticleNotificationsOpen)).toBool();
}

void ArticleListNotification::rebuildTorrentActions() {
  while (QLayoutItem* item = m_torrentActionsLayout->takeAt(0)) {
    delete item->widget();
    delete item;
  }

  const QList<Message> messages = selectedMessages();
  const bool hasTorrent = !messages.isEmpty() && !TorrentExtractor::extract(messages).urls.isEmpty();

  const QList<TorrentClientConfig> clients = TorrentClientConfig::load(qApp->settings());
  int buttonIndex = 0;
  for (const TorrentClientConfig& config : clients) {
    auto* button = new QPushButton(config.name, this);
    button->setMinimumHeight(qMax(32, button->sizeHint().height()));
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    button->setEnabled(hasTorrent);
    button->setToolTip(hasTorrent ? tr("Send the selected article torrent(s) to %1").arg(config.name)
                                  : tr("No torrent link found in the selected article(s)"));
    connect(button, &QPushButton::clicked, this, [this, config]() { sendSelectedToTorrentClient(config); });
    m_torrentActionsLayout->addWidget(button, buttonIndex / 2, buttonIndex % 2);
    ++buttonIndex;
  }
}

void ArticleListNotification::sendSelectedToTorrentClient(const TorrentClientConfig& config) {
  const TorrentExtractionResult extraction = TorrentExtractor::extract(selectedMessages());

  if (extraction.urls.isEmpty()) {
    QMessageBox::warning(this,
                         tr("No torrent links found"),
                         tr("No magnet link, .torrent URL, or torrent enclosure was found in the selected article(s)."));
    return;
  }

  stopTimedClosing();
  TorrentClient* client = TorrentClient::create(config, this);
  connect(client, &TorrentClient::addFinished, this, [this, client](int added, int failed, const QString& message) {
    if (failed == 0 && qApp->settings()->value(QStringLiteral("TorrentClients"),
                                               QStringLiteral("showSuccessNotifications"),
                                               true).toBool()) {
      QMessageBox box(QMessageBox::Information, tr("Torrents sent"), message, QMessageBox::Ok, this);
      auto* dontShowAgain = new QCheckBox(tr("Don't show successful-send confirmations again"), &box);
      box.setCheckBox(dontShowAgain);
      box.exec();
      if (dontShowAgain->isChecked()) {
        qApp->settings()->setValue(QStringLiteral("TorrentClients"), QStringLiteral("showSuccessNotifications"), false);
      }
    }
    else if (failed > 0) {
      QMessageBox::warning(this, added > 0 ? tr("Some torrents were not sent") : tr("Torrents were not sent"), message);
    }
    client->deleteLater();
    setupTimedClosing(false);
  });
  client->addTorrents(extraction.urls);
}

void ArticleListNotification::showFeed(int index) {
  Q_UNUSED(index)
  m_model->setArticles(m_newMessages.value(selectedFeed()));
  onMessageSelected({}, {});
}

void ArticleListNotification::openArticleInWebBrowser() {
  Feed* fd = selectedFeed();
  const QModelIndex current_index = m_ui.m_treeArticles->currentIndex();
  Message& msg = selectedMessage();

  if (!msg.m_isRead) {
    const int message_id = msg.m_id;

    markAsRead(fd, {msg});
    m_model->setMessageRead(current_index, true);

    const auto cached_messages = m_newMessages.find(fd);

    if (cached_messages != m_newMessages.end()) {
      for (Message& cached_message : cached_messages.value()) {
        if (cached_message.m_id == message_id) {
          cached_message.m_isRead = true;
          break;
        }
      }
    }

    emit oneArticleSetReadUnreadById(message_id, RootItem::ReadStatus::Read);
  }

  if (qApp->web()->openUrlInExternalBrowser(msg.m_url, true) && m_newMessages.size() == 1 &&
      m_newMessages.value(m_newMessages.keys().at(0)).size() == 1) {
    // We only have 1 message in 1 feed.
    emit closeRequested(this, false);
  }
}

void ArticleListNotification::markAllRead() {
  bool any_message_marked_read = false;

  for (auto it = m_newMessages.begin(); it != m_newMessages.end(); ++it) {
    QList<Message> unread_messages;

    for (const Message& message : std::as_const(it.value())) {
      if (!message.m_isRead) {
        unread_messages.append(message);
      }
    }

    if (unread_messages.isEmpty()) {
      continue;
    }

    markAsRead(it.key(), unread_messages);

    for (Message& message : it.value()) {
      message.m_isRead = true;
    }

    any_message_marked_read = true;
  }

  if (any_message_marked_read) {
    showFeed(m_ui.m_cmbFeeds->currentIndex());
    emit dataChangeNotificationTriggered(selectedFeed(), FeedsModel::ExternalDataChange::MarkedAllReadFromNotification);
  }
}

void ArticleListNotification::markAsRead(Feed* feed, const QList<Message>& articles) {
  ServiceRoot* acc = feed->account();
  QStringList message_ids;
  message_ids.reserve(articles.size());

  // Obtain IDs of all desired messages.
  for (const Message& message : articles) {
    message_ids.append(QString::number(message.m_id));
  }

  acc->onBeforeSetMessagesRead(feed, articles, RootItem::ReadStatus::Read);

  qApp->database()->worker()->write([&](const QSqlDatabase& db) {
    DatabaseQueries::markMessagesReadUnread(db, message_ids, RootItem::ReadStatus::Read);
  });

  acc->onAfterSetMessagesRead(feed, articles, RootItem::ReadStatus::Read);
}

Feed* ArticleListNotification::selectedFeed(int index) const {
  if (index < 0) {
    return m_ui.m_cmbFeeds->currentData().value<Feed*>();
  }
  else {
    return m_ui.m_cmbFeeds->itemData(index).value<Feed*>();
  }
}

Message& ArticleListNotification::selectedMessage() {
  if (m_ui.m_treeArticles->currentIndex().isValid()) {
    return m_model->message(m_ui.m_treeArticles->currentIndex());
  }
  else {
    throw ApplicationException(QSL("message cannot be loaded, wrong index"));
  }
}

QList<Message> ArticleListNotification::selectedMessages() const {
  QList<Message> messages;
  QModelIndexList rows = m_ui.m_treeArticles->selectionModel()->selectedRows();

  if (rows.isEmpty() && m_ui.m_treeArticles->currentIndex().isValid()) {
    rows.append(m_ui.m_treeArticles->currentIndex());
  }

  std::sort(rows.begin(), rows.end(), [](const QModelIndex& lhs, const QModelIndex& rhs) {
    return lhs.row() < rhs.row();
  });

  messages.reserve(rows.size());
  for (const QModelIndex& row : std::as_const(rows)) {
    messages.append(m_model->message(row));
  }

  return messages;
}

bool ArticleListNotification::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::Type::Wheel &&
      (watched == m_ui.m_treeArticles || watched == m_ui.m_treeArticles->viewport())) {
    auto* wheel_event = static_cast<QWheelEvent*>(event);
    const int vertical_delta =
      !wheel_event->angleDelta().isNull() ? wheel_event->angleDelta().y() : wheel_event->pixelDelta().y();

    if (vertical_delta != 0) {
      if (vertical_delta > 0) {
        m_model->previousPage();
      }
      else {
        m_model->nextPage();
      }

      wheel_event->accept();
      return true;
    }
  }

  if (event->type() == QEvent::Type::MouseButtonRelease) {
    auto* mouse_event = dynamic_cast<QMouseEvent*>(event);

    if (mouse_event->button() == Qt::MouseButton::MiddleButton && watched == m_ui.m_treeArticles->viewport()) {
      const QModelIndex clicked_index = m_ui.m_treeArticles->indexAt(mouse_event->pos());

      if (clicked_index.isValid()) {
        m_ui.m_treeArticles->setCurrentIndex(clicked_index);
        openArticleInArticleList();
      }
    }
  }

  return BaseToastNotification::eventFilter(watched, event);
}
