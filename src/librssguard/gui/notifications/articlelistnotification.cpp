// For license of this file, see <project-root-folder>/LICENSE.md.

#include "gui/notifications/articlelistnotification.h"

#include "core/articlelistnotificationmodel.h"
#include "database/databasefactory.h"
#include "database/databasequeries.h"
#include "miscellaneous/iconfactory.h"
#include "miscellaneous/localization.h"
#include "miscellaneous/settings.h"
#include "miscellaneous/settingskeys.h"
#include "network-web/webfactory.h"
#include "torrent/torrentclient.h"
#include "torrent/torrentclientconfig.h"
#include "torrent/torrentautomationengine.h"
#include "torrent/torrentautomationconfig.h"
#include "torrent/torrentextractor.h"
#include "torrent/torrentsendhistory.h"

#include <QCheckBox>
#include <QColor>
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
  m_countdownTimer.setInterval(1000);
  connect(&m_countdownTimer, &QTimer::timeout, this, [this]() {
    if (m_countdownSeconds > 0) --m_countdownSeconds;
    if (m_receivedStatus != nullptr) {
      const QString received = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
      m_receivedStatus->setText(m_countdownSeconds > 0
        ? tr("Received %1 · closes in %2s").arg(received).arg(m_countdownSeconds)
        : tr("Received %1").arg(received));
    }
  });

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
  connect(TorrentSendHistory::instance(qApp),
          &TorrentSendHistory::historyChanged,
          this,
          [this](const QList<int>& messageIds, const QString&) {
            for (int messageId : messageIds) m_model->setMessageProcessed(messageId);
            rebuildTorrentActions();
          });
  connect(TorrentAutomationEngine::instance(qApp), &TorrentAutomationEngine::busyChanged, this, [this](bool busy) {
    if (!busy) { rebuildTorrentActions(); setupTimedClosing(false); }
  });
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

void ArticleListNotification::loadPreview(bool includeTorrentButtons) {
  m_preview = true;
  m_previewTorrentButtons = includeTorrentButtons;
  setupTimedClosing(false);
  m_countdownSeconds = notificationTimeoutSeconds();
  if (m_countdownSeconds <= 0 && !staysOpenUntilDismissed())
    m_countdownSeconds = qApp->settings()->value(GROUP(GUI), SETTING(GUI::ToastNotificationsDuration)).toInt();
  if (m_countdownSeconds > 0) m_countdownTimer.start();
  m_ui.m_cmbFeeds->clear();
  m_ui.m_cmbFeeds->addItem(tr("Example RSS feed"));
  m_ui.m_lblTitle->setText(tr("1 feed fetched"));
  Message example;
  example.m_title = tr("Example new article with a torrent link");
  example.m_url = QStringLiteral("magnet:?xt=urn:btih:0000000000000000000000000000000000000000");
  m_model->setArticles({example});
  if (m_model->rowCount({}) > 0) {
    const QModelIndex first = m_model->index(0, 0);
    m_ui.m_treeArticles->selectionModel()->select(first,
                                                  QItemSelectionModel::SelectionFlag::ClearAndSelect |
                                                    QItemSelectionModel::SelectionFlag::Rows);
    m_ui.m_treeArticles->setCurrentIndex(first);
  }
  m_ui.m_btnOpenArticleList->setEnabled(false);
  m_ui.m_btnOpenWebBrowser->setEnabled(false);
  m_ui.m_btnMarkAllRead->setEnabled(false);
  rebuildTorrentActions();
}

bool ArticleListNotification::staysOpenUntilDismissed() const {
  if (TorrentAutomationConfig::load(qApp->settings()).notificationDurationSeconds > 0) return false;
  return qApp->settings()->value(GROUP(GUI), SETTING(GUI::KeepArticleNotificationsOpen)).toBool();
}

int ArticleListNotification::notificationTimeoutSeconds() const {
  return TorrentAutomationConfig::load(qApp->settings()).notificationDurationSeconds;
}

void ArticleListNotification::rebuildTorrentActions() {
  while (QLayoutItem* item = m_torrentActionsLayout->takeAt(0)) {
    delete item->widget();
    delete item;
  }

  const QList<Message> messages = selectedMessages();
  const bool hasTorrent = !messages.isEmpty() && !TorrentExtractor::extract(messages).urls.isEmpty();

  const QList<TorrentClientConfig> clients =
    TorrentClientConfig::enabledInPriorityOrder(TorrentClientConfig::load(qApp->settings()));
  TorrentSendHistory* history = TorrentSendHistory::instance(qApp);
  if (m_preview && !m_previewTorrentButtons) return;
  const TorrentAutomationConfig automation = TorrentAutomationConfig::load(qApp->settings());
  m_receivedStatus = new QLabel(tr("Received %1%2").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm")),
    m_countdownSeconds > 0 ? tr(" · closes in %1s").arg(m_countdownSeconds) : QString()), this);
  m_receivedStatus->setToolTip(tr("The local time this notification was received. A date is shown when it differs from today."));
  auto* pause = new QPushButton(automation.paused ? tr("Resume automation") : tr("Pause automation"), this);
  auto* silent = new QPushButton(tr("Go silent this session"), this);
  pause->setToolTip(tr("Pause new automatic sends, cleanup, retention and retries. Existing transfers continue."));
  silent->setToolTip(tr("Suppress later torrent popups until RSS Guard is restarted. Automation and logging continue."));
  if (!m_preview) {
    connect(pause, &QPushButton::clicked, this, [this, pause]() {
      TorrentAutomationEngine* engine = TorrentAutomationEngine::instance(qApp);
      engine->setPaused(!engine->paused());
      pause->setText(engine->paused() ? tr("Resume automation") : tr("Pause automation"));
    });
    connect(silent, &QPushButton::clicked, this, [this]() {
      qApp->setProperty("torrentSessionSilent", true);
      closeNotification();
    });
  }
  m_torrentActionsLayout->addWidget(m_receivedStatus, 0, 0);
  m_torrentActionsLayout->addWidget(pause, 0, 1);
  m_torrentActionsLayout->addWidget(silent, 1, 0, 1, 2);
  auto* storage = new QLabel(TorrentAutomationEngine::instance(qApp)->storageOverview(), this);
  storage->setWordWrap(true);
  storage->setToolTip(tr("Live values come from the client API. Estimates and unavailable values are clearly labelled."));
  auto* reviewSpace = new QPushButton(tr("Review space cleanup (dry run)"), this);
  reviewSpace->setToolTip(tr("Run a read-only assessment. Existing protections still apply and nothing is deleted."));
  if (!m_preview) connect(reviewSpace, &QPushButton::clicked, this, []() {
    TorrentAutomationEngine::instance(qApp)->runDryTest();
  });
  m_torrentActionsLayout->addWidget(storage, 2, 0);
  m_torrentActionsLayout->addWidget(reviewSpace, 2, 1);
  bool allAlreadyProcessed = !messages.isEmpty();
  for (const Message& message : messages)
    if (!history->wasSentToAnyClient(message.m_id)) { allAlreadyProcessed = false; break; }
  auto* automatic = new QPushButton(allAlreadyProcessed ? tr("✓ Processed automatically") : tr("Process automatically"), this);
  automatic->setMinimumHeight(36);
  automatic->setCursor(Qt::PointingHandCursor);
  automatic->setStyleSheet(allAlreadyProcessed
    ? QStringLiteral("QPushButton { background:#2eaf55; color:white; border:2px solid #19733a; border-radius:3px; padding:6px; }")
    : QStringLiteral("QPushButton { background:#1877d2; color:white; border:2px outset #1877d2; border-radius:3px; padding:6px; font-weight:600; } QPushButton:pressed { background:#0f4f91; border-style:inset; padding-top:8px; }"));
  automatic->setEnabled(m_preview || (hasTorrent && !allAlreadyProcessed));
  automatic->setToolTip(tr("Assess live capacity, download load, priority and health, then choose or override the recommended torrent client."));
  if (!m_preview) connect(automatic, &QPushButton::clicked, this, [this, automatic, messages]() {
    automatic->setText(tr("Assessing torrent clients…"));
    automatic->setEnabled(false);
    stopTimedClosing();
    TorrentAutomationEngine::processApprovedArticles(selectedFeed(), messages, this, qApp);
  });
  m_torrentActionsLayout->addWidget(automatic, 3, 0, 1, 2);
  int buttonIndex = 0;
  for (const TorrentClientConfig& config : clients) {
    bool alreadySent = !messages.isEmpty();
    for (const Message& message : messages) {
      if (!history->wasSent(message.m_id, config.id)) {
        alreadySent = false;
        break;
      }
    }
    auto* button = new QPushButton(alreadySent ? tr("✓ %1").arg(config.name) : config.name, this);
    button->setFlat(false);
    button->setAutoDefault(false);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(qMax(32, button->sizeHint().height()));
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    const QColor buttonColor(alreadySent ? QStringLiteral("#2eaf55")
                                         : (config.colorNotificationButtons ? config.buttonColor : QString()));
    if (buttonColor.isValid()) {
      const QString textColor = buttonColor.lightness() < 145 ? QStringLiteral("#ffffff") : QStringLiteral("#111111");
      const QColor hoverColor = buttonColor.lighter(112);
      const QColor pressedColor = buttonColor.darker(135);
      button->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; color: %2; border: 2px outset %1; border-radius: 3px; padding: 5px 9px; }"
        "QPushButton:hover { background-color: %3; }"
        "QPushButton:pressed { background-color: %4; border-style: inset; padding-top: 7px; padding-left: 11px; }"
        "QPushButton:disabled { background-color: #b0b0b0; color: #666666; border-color: #999999; }")
          .arg(buttonColor.name(), textColor, hoverColor.name(), pressedColor.name()));
    }
    else button->setStyleSheet(QStringLiteral(
      "QPushButton { border: 2px outset palette(mid); border-radius: 3px; padding: 5px 9px; background: palette(button); }"
      "QPushButton:hover { background: palette(light); }"
      "QPushButton:pressed { background: palette(dark); border-style: inset; padding-top: 7px; padding-left: 11px; }"));
    if (alreadySent) {
      button->setStyleSheet(QStringLiteral(
        "QPushButton, QPushButton:disabled { background-color: #2eaf55; color: #ffffff; "
        "border: 2px solid #19733a; border-radius: 3px; padding: 5px 9px; }"));
    }
    button->setEnabled(m_preview || (hasTorrent && !alreadySent));
    button->setToolTip(alreadySent
                         ? tr("The selected article torrent(s) were already sent successfully to %1").arg(config.name)
                         : (hasTorrent ? tr("Send the selected article torrent(s) to %1").arg(config.name)
                                       : tr("No torrent link found in the selected article(s)")));
    if (m_preview) button->setToolTip(tr("Preview: %1 (priority %2)").arg(config.name).arg(config.priority));
    else connect(button, &QPushButton::clicked, this, [this, config, button]() {
      button->setText(tr("Sending to %1…").arg(config.name));
      button->setEnabled(false);
      sendSelectedToTorrentClient(config);
    });
    m_torrentActionsLayout->addWidget(button, 4 + buttonIndex / 2, buttonIndex % 2);
    ++buttonIndex;
  }
}

void ArticleListNotification::sendSelectedToTorrentClient(const TorrentClientConfig& config) {
  const QList<Message> selected = selectedMessages();
  const TorrentExtractionResult extraction = TorrentExtractor::extract(selected);

  if (extraction.urls.isEmpty()) {
    QMessageBox::warning(this,
                         tr("No torrent links found"),
                         tr("No magnet link, .torrent URL, or torrent enclosure was found in the selected article(s)."));
    rebuildTorrentActions();
    return;
  }
  stopTimedClosing();
  TorrentAutomationEngine::processDirectArticles(config, selected, qApp);
}

void ArticleListNotification::showFeed(int index) {
  Q_UNUSED(index)
  if (m_preview) return;
  m_model->setArticles(m_newMessages.value(selectedFeed()));
  for (const Message& message : m_newMessages.value(selectedFeed()))
    if (TorrentSendHistory::instance(qApp)->wasSentToAnyClient(message.m_id)) m_model->setMessageProcessed(message.m_id);
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
