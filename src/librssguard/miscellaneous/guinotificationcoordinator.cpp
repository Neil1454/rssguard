// For license of this file, see <project-root-folder>/LICENSE.md.

#include "miscellaneous/guinotificationcoordinator.h"

#include "core/feeddownloader.h"
#include "core/feedsmodel.h"
#include "core/messagesmodel.h"
#include "gui/dialogs/formabout.h"
#include "gui/dialogs/formmain.h"
#include "gui/feedmessageviewer.h"
#include "gui/messagebox.h"
#include "gui/messagesview.h"
#include "gui/notifications/toastnotificationsmanager.h"
#include "gui/toolbars/statusbar.h"
#include "gui/tray/qttrayicon.h"
#include "miscellaneous/application.h"
#include "miscellaneous/feedreader.h"
#include "miscellaneous/iconfactory.h"
#include "miscellaneous/notificationfactory.h"
#include "miscellaneous/settings.h"
#include "miscellaneous/settingskeys.h"
#include "qtlinq/qtlinq.h"
#include "services/abstract/feed.h"
#include "torrent/torrentautomationengine.h"
#include "torrent/torrentextractor.h"

#if defined(Q_OS_WIN)
#include "miscellaneous/windowstaskbar.h"
#endif

#include <QGuiApplication>
#include <QMetaObject>
#include <QPixmap>
#include <QTimer>
#include <QVariantMap>

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
#include <QDBusConnection>
#include <QDBusMessage>
#endif

#if defined(Q_OS_WIN) && QT_VERSION_MAJOR == 6
#include <QWindow>
#include <QtGui/qpa/qplatformwindow_p.h>
#endif

GuiNotificationCoordinator::GuiNotificationCoordinator(Application* application)
  : QObject(), m_application(application), m_trayIcon(nullptr) {
  // Start the lightweight maintenance timer even when no feed refresh has yet
  // produced new articles. This allows maximum-retention deadlines to be
  // checked while RSS Guard is simply left running.
  QTimer::singleShot(0, this, [application]() { TorrentAutomationEngine::instance(application); });
  m_exclusiveTimer = new QTimer(this);
  m_exclusiveTimer->setInterval(5000);
  connect(m_exclusiveTimer, &QTimer::timeout, this, &GuiNotificationCoordinator::updateExclusiveMode);
  m_exclusiveTimer->start();
  QTimer::singleShot(0, this, &GuiNotificationCoordinator::updateExclusiveMode);
}

void GuiNotificationCoordinator::storeExclusiveStatus(const QString& state) {
  m_application->settings()->setValue(QStringLiteral("TorrentAutomation"),
                                      QStringLiteral("exclusiveRuntimeState"), state);
  m_application->settings()->setValue(QStringLiteral("TorrentAutomation"),
                                      QStringLiteral("exclusiveCollectedCount"), m_exclusiveArticleKeys.size());
  m_application->settings()->setValue(QStringLiteral("TorrentAutomation"),
                                      QStringLiteral("exclusiveNextWakeUtc"),
                                      m_exclusiveNextWake.toUTC().toString(Qt::ISODate));
}

void GuiNotificationCoordinator::enterExclusiveSleep(const QString& detail) {
  const TorrentAutomationConfig config = TorrentAutomationConfig::load(m_application->settings());
  m_exclusiveState = ExclusiveState::Sleeping;
  m_exclusiveCycleStart = {};
  m_exclusiveCutoff = {};
  m_exclusiveMonitorEnd = {};
  m_exclusiveNextPoll = {};
  m_exclusiveArticles.clear();
  m_exclusiveArticleKeys.clear();
  m_exclusiveDispatchStarted = false;
  m_exclusiveNextWake = QDateTime::currentDateTimeUtc().addSecs(qMax(1, config.exclusiveSleepMinutes) * 60);
  storeExclusiveStatus(tr("Sleeping"));
  TorrentAutomationEngine::recordExclusiveState(QStringLiteral("exclusive-sleeping"),
    detail + tr(" Next wake: %1.").arg(m_exclusiveNextWake.toLocalTime().toString(QStringLiteral("dd/MM/yyyy HH:mm"))),
    m_application);
}

void GuiNotificationCoordinator::startExclusiveCycle() {
  if (m_application->feedReader() == nullptr || m_application->feedReader()->isFeedUpdateRunning()) return;
  const TorrentAutomationConfig config = TorrentAutomationConfig::load(m_application->settings());
  m_exclusiveState = ExclusiveState::Baselining;
  m_exclusiveCycleStart = QDateTime::currentDateTimeUtc();
  m_exclusiveCutoff = m_exclusiveCycleStart.addSecs(-qMax(0, config.exclusiveFreshnessMinutes) * 60);
  m_exclusiveMonitorEnd = {};
  m_exclusiveNextPoll = {};
  m_exclusiveNextWake = {};
  m_exclusiveArticles.clear();
  m_exclusiveArticleKeys.clear();
  storeExclusiveStatus(tr("Baselining"));
  TorrentAutomationEngine::recordExclusiveState(QStringLiteral("exclusive-baselining"),
    tr("Exclusive cycle woke at %1. Existing items older than %2 are ignored completely; normal routing, retries, retention and cleanup remain frozen.")
      .arg(m_exclusiveCycleStart.toLocalTime().toString(QStringLiteral("dd/MM/yyyy HH:mm:ss")),
           m_exclusiveCutoff.toLocalTime().toString(QStringLiteral("dd/MM/yyyy HH:mm:ss"))),
    m_application);
  m_application->feedReader()->updateAllFeeds();
}

void GuiNotificationCoordinator::collectExclusiveArticles(const QHash<Feed*, QList<Message>>& articles,
                                                           bool baseline) {
  const QDateTime now = QDateTime::currentDateTimeUtc();
  for (auto feedIt = articles.constBegin(); feedIt != articles.constEnd(); ++feedIt) {
    for (const Message& message : feedIt.value()) {
      if (TorrentExtractor::extract(message).isEmpty()) continue;
      const QDateTime published = message.m_created.toUTC();
      const bool reliablePublished = message.m_createdFromFeed && published.isValid();
      const bool withinFreshness = reliablePublished && published >= m_exclusiveCutoff && published <= now.addSecs(300);
      Q_UNUSED(baseline)
      // Fail closed: retrieval time only tells us when RSS Guard saw an item,
      // not when it was published. Without a trustworthy feed timestamp the
      // mode cannot prove freshness, so the item is ignored.
      if (!withinFreshness) continue;
      const QString key = !message.m_customId.isEmpty()
                            ? message.m_customId
                            : QStringLiteral("%1|%2|%3").arg(message.m_feedCustomId,
                                                               message.m_url,
                                                               message.m_title);
      if (m_exclusiveArticleKeys.contains(key)) continue;
      m_exclusiveArticleKeys.insert(key);
      m_exclusiveArticles[feedIt.key()].append(message);
    }
  }
  storeExclusiveStatus(m_exclusiveState == ExclusiveState::Baselining ? tr("Baselining") : tr("Collecting"));
}

void GuiNotificationCoordinator::beginExclusiveDispatch(const QString& reason) {
  const TorrentAutomationConfig config = TorrentAutomationConfig::load(m_application->settings());
  if (m_exclusiveArticleKeys.isEmpty()) {
    enterExclusiveSleep(tr("The monitoring window ended without a fresh torrent release, so nothing was sent."));
    return;
  }
  if (m_exclusiveArticleKeys.size() < config.exclusiveBatchSize && !config.exclusiveSendPartialBatch) {
    enterExclusiveSleep(tr("The monitoring window ended with %1 of %2 releases. Partial-batch sending is disabled, so the batch was discarded.")
                          .arg(m_exclusiveArticleKeys.size()).arg(config.exclusiveBatchSize));
    return;
  }
  m_exclusiveState = ExclusiveState::Sending;
  m_exclusiveDispatchStarted = false;
  storeExclusiveStatus(tr("Sending"));
  TorrentAutomationEngine::recordExclusiveState(QStringLiteral("exclusive-sending"),
    tr("%1 Sending %2 fresh release(s) using the enabled clients' configured routing priorities. No deletion or cleanup can run.")
      .arg(reason).arg(m_exclusiveArticleKeys.size()), m_application);
}

void GuiNotificationCoordinator::handleExclusiveFeedResults(const FeedDownloadResults& results) {
  const TorrentAutomationConfig config = TorrentAutomationConfig::load(m_application->settings());
  if (!config.exclusiveModeEnabled || !config.exclusiveModeArmed) return;
  if (m_exclusiveState == ExclusiveState::Baselining) {
    collectExclusiveArticles(results.updatedFeeds(), true);
    m_exclusiveState = ExclusiveState::Collecting;
    m_exclusiveMonitorEnd = QDateTime::currentDateTimeUtc().addSecs(qMax(1, config.exclusiveMonitoringMinutes) * 60);
    m_exclusiveNextPoll = QDateTime::currentDateTimeUtc().addSecs(qMax(1, config.exclusivePollMinutes) * 60);
    storeExclusiveStatus(tr("Collecting"));
    TorrentAutomationEngine::recordExclusiveState(QStringLiteral("exclusive-collecting"),
      tr("Baseline complete. %1 fresh release(s) qualified. Monitoring until %2 or until %3 release(s) are collected.")
        .arg(m_exclusiveArticleKeys.size())
        .arg(m_exclusiveMonitorEnd.toLocalTime().toString(QStringLiteral("dd/MM/yyyy HH:mm:ss")))
        .arg(config.exclusiveBatchSize), m_application);
  }
  else if (m_exclusiveState == ExclusiveState::Collecting) {
    collectExclusiveArticles(results.updatedFeeds(), false);
  }
  if (m_exclusiveState == ExclusiveState::Collecting &&
      m_exclusiveArticleKeys.size() >= config.exclusiveBatchSize)
    beginExclusiveDispatch(tr("The target batch size was reached."));
}

void GuiNotificationCoordinator::updateExclusiveMode() {
  const TorrentAutomationConfig config = TorrentAutomationConfig::load(m_application->settings());
  if (!config.exclusiveModeEnabled || !config.exclusiveModeArmed) {
    if (m_exclusiveState != ExclusiveState::Disabled) {
      m_exclusiveState = ExclusiveState::Disabled;
      m_exclusiveArticles.clear();
      m_exclusiveArticleKeys.clear();
      m_exclusiveNextWake = {};
      storeExclusiveStatus(tr("Disabled"));
      TorrentAutomationEngine::recordExclusiveState(QStringLiteral("exclusive-disabled"),
        tr("Exclusive Batch Mode was disabled. Normal unattended automation and pending retries may resume."),
        m_application);
    }
    return;
  }
  const QDateTime now = QDateTime::currentDateTimeUtc();
  if (m_exclusiveState == ExclusiveState::Disabled) {
    enterExclusiveSleep(tr("Exclusive Batch Mode enabled. Normal unattended automation is frozen for the whole sleep/collect/send cycle."));
    return;
  }
  if (m_exclusiveState == ExclusiveState::Sleeping && now >= m_exclusiveNextWake) {
    startExclusiveCycle();
    return;
  }
  if (m_exclusiveState == ExclusiveState::Collecting) {
    if (now >= m_exclusiveMonitorEnd) {
      beginExclusiveDispatch(tr("The monitoring time expired."));
      return;
    }
    if (now >= m_exclusiveNextPoll && m_application->feedReader() != nullptr &&
        !m_application->feedReader()->isFeedUpdateRunning()) {
      m_exclusiveNextPoll = now.addSecs(qMax(1, config.exclusivePollMinutes) * 60);
      m_application->feedReader()->updateAllFeeds();
    }
    return;
  }
  if (m_exclusiveState == ExclusiveState::Sending) {
    TorrentAutomationEngine* engine = TorrentAutomationEngine::instance(m_application);
    if (!m_exclusiveDispatchStarted && !engine->busy()) {
      m_exclusiveDispatchStarted = true;
      TorrentAutomationEngine::processExclusiveArticles(m_exclusiveArticles, m_application);
    }
    else if (m_exclusiveDispatchStarted && !engine->busy()) {
      enterExclusiveSleep(tr("The fresh exclusive batch finished processing."));
    }
  }
}

GuiNotificationCoordinator::~GuiNotificationCoordinator() {
  delete m_trayIcon;
}

TrayIcon* GuiNotificationCoordinator::trayIcon() {
  if (m_trayIcon == nullptr) {
    QPixmap tray_icon;
    QPixmap tray_icon_unread;
    QPixmap tray_icon_paused;

    const bool monochrome_icon =
      m_application->settings()->value(GROUP(GUI), SETTING(GUI::MonochromeTrayIcon)).toBool();
    const bool custom_colored_icon =
      m_application->settings()->value(GROUP(GUI), SETTING(GUI::CustomColoredTrayIcon)).toBool();
    const bool colored_unread_icon =
      m_application->settings()->value(GROUP(GUI), SETTING(GUI::ColoredBusyTrayIcon)).toBool();
    const bool show_unread_count =
      m_application->settings()->value(GROUP(GUI), SETTING(GUI::UnreadNumbersInTrayIcon)).toBool();
    QColor unread_text_color(Qt::GlobalColor::white);

    if (custom_colored_icon) {
      QColor background_color(m_application->settings()
                                ->value(GROUP(GUI), SETTING(GUI::CustomColoredTrayIconBackground))
                                .toString());
      unread_text_color =
        QColor(m_application->settings()->value(GROUP(GUI), SETTING(GUI::CustomColoredTrayIconText)).toString());

      if (m_application->icons()->ensureCustomColoredIcons(background_color)) {
        tray_icon = QPixmap(m_application->icons()->customColoredTrayIconPath());
        tray_icon_unread =
          show_unread_count ? QPixmap(m_application->icons()->customColoredTrayIconUnreadPath()) : tray_icon;
        tray_icon_paused = QPixmap(m_application->icons()->customColoredTrayIconUnreadPath());
      }
    }

    if (tray_icon.isNull() || tray_icon_unread.isNull() || tray_icon_paused.isNull()) {
      unread_text_color = QColor(Qt::GlobalColor::white);

      if (!custom_colored_icon && monochrome_icon) {
        tray_icon = QPixmap(APP_ICON_MONO_PATH);
        tray_icon_unread = colored_unread_icon
                             ? (show_unread_count ? QPixmap(APP_ICON_UNREAD_PATH) : QPixmap(APP_ICON_PATH))
                             : (show_unread_count ? QPixmap(APP_ICON_MONO_UNREAD_PATH) : QPixmap(APP_ICON_MONO_PATH));
        tray_icon_paused = QPixmap(APP_ICON_MONO_UNREAD_PATH);
      }
      else {
        tray_icon = QPixmap(APP_ICON_PATH);
        tray_icon_unread = show_unread_count ? QPixmap(APP_ICON_UNREAD_PATH) : QPixmap(APP_ICON_PATH);
        tray_icon_paused = QPixmap(APP_ICON_UNREAD_PATH);
      }
    }

    m_trayIcon = new QtTrayIcon(QSL(APP_LOW_NAME),
                                QSL(APP_NAME),
                                tray_icon,
                                tray_icon_unread,
                                tray_icon_paused,
                                unread_text_color,
                                m_application->mainForm());
    m_trayIcon->setMainWindow(m_application->mainForm());
    m_trayIcon->setContextMenu(m_application->mainForm()->trayMenu());
    m_trayIcon->setToolTip(QSL(APP_NAME));

    connect(m_trayIcon, &TrayIcon::activated, m_application->mainForm(), [this]() {
      m_application->mainForm()->switchVisibility();
    });
    connect(m_trayIcon, &TrayIcon::shown, m_application->feedReader()->feedsModel(), &FeedsModel::notifyWithCounts);
  }

  return m_trayIcon;
}

void GuiNotificationCoordinator::setMainForm() {
  if (auto* toasts = m_application->toastNotifications(); toasts != nullptr) {
    connect(toasts,
            &ToastNotificationsManager::dataChangeNotificationTriggered,
            m_application->mainForm()->tabWidget()->feedMessageViewer()->messagesView(),
            &MessagesView::reactOnExternalDataChange);
    connect(toasts,
            &ToastNotificationsManager::oneArticleSetReadUnreadById,
            m_application->mainForm()->tabWidget()->feedMessageViewer()->messagesView()->sourceModel(),
            &MessagesModel::setMessageReadById);
  }
}

void GuiNotificationCoordinator::showTrayIcon() {
  if (TrayIcon::isSystemTrayDesired()) {
    qDebugNN << LOGSEC_GUI << "User wants to have tray icon.";
    qWarningNN << LOGSEC_GUI << "Showing tray icon with little delay.";

    QTimer::singleShot(
#if defined(Q_OS_WIN)
      500,
#else
      3000,
#endif
      this,
      [this]() {
        if (trayIcon()->isAvailable()) {
          qWarningNN << LOGSEC_GUI << "Tray icon is available, showing now.";
          trayIcon()->show();
          QGuiApplication::setQuitOnLastWindowClosed(false);
        }
        else {
          m_application->feedReader()->feedsModel()->notifyWithCounts();
        }

        offerChanges();
        offerPolls();

#if defined(Q_OS_WIN) && QT_VERSION_MAJOR == 6
        using QWindowsWindow = QNativeInterface::Private::QWindowsWindow;
        if (auto window = m_application->mainForm()->windowHandle()->nativeInterface<QWindowsWindow>()) {
          window->setHasBorderInFullScreen(true);
        }
#endif
      });
  }
  else {
    m_application->feedReader()->feedsModel()->notifyWithCounts();
  }
}

void GuiNotificationCoordinator::deleteTrayIcon() {
  if (m_trayIcon != nullptr) {
    qDebugNN << LOGSEC_CORE << "Disabling tray icon, deleting it and raising main application window.";
    m_application->mainForm()->display();
    delete m_trayIcon;
    m_trayIcon = nullptr;
    QGuiApplication::setQuitOnLastWindowClosed(true);
  }
}

void GuiNotificationCoordinator::offerChanges() {
  if (m_application->isFirstRunCurrentVersion()) {
    const QString welcomeText = m_application->isFirstRun()
      ? m_application
          ->tr("Welcome to %1.\n\nOriginal RSS Guard application created by %2.\n"
               "Torrent-related features conceived and directed by %3 (%4).\n\n"
               "Open the changelog to see what is included in this version.")
          .arg(QSL(APP_LONG_NAME), QSL(APP_AUTHOR), QSL(TORRENT_FEATURE_AUTHOR), QSL(TORRENT_FEATURE_EMAIL))
      : m_application
          ->tr("Welcome to %1.\n\nPlease, check NEW stuff included in this\n"
               "version by clicking this popup notification.")
          .arg(QSL(APP_LONG_NAME));
    showGuiMessage(Notification::Event::GeneralEvent,
                   {m_application->tr("Welcome"),
                    welcomeText,
                    QSystemTrayIcon::MessageIcon::Information},
                   {},
                   GuiAction(m_application->tr("Go to changelog"),
                             m_application->mainForm(),
                             [this]() {
                               FormAbout(true, m_application->mainForm()).exec();
                             }),
                   nullptr);
  }
}

void GuiNotificationCoordinator::offerPolls() const {
  // Reserved for version-specific polls.
}

void GuiNotificationCoordinator::showGuiMessage(Notification::Event event,
                                                const GuiMessage& message,
                                                const GuiMessageDestination& destination,
                                                const GuiAction& action,
                                                QWidget* parent) {
  QMetaObject::invokeMethod(
    this,
    [this, event, message, destination, action, parent]() {
      showGuiMessageCore(event, message, destination, action, parent);
    },
    Qt::ConnectionType::QueuedConnection);
}

void GuiNotificationCoordinator::showGuiMessageCore(Notification::Event event,
                                                    const GuiMessage& message,
                                                    const GuiMessageDestination& destination,
                                                    const GuiAction& action,
                                                    QWidget* parent) {
  bool show_dialog = true;

  if (m_application->notifications()->areNotificationsEnabled()) {
    auto notification = m_application->notifications()->notificationForEvent(event);
    show_dialog = notification.dialogEnabled();

    if (notification.soundEnabled()) {
      notification.playSound(m_application);
    }

    if (notification.balloonEnabled() && destination.m_tray) {
      if (notification.event() == Notification::Event::ArticlesFetchingStarted &&
          m_application->mainForm() != nullptr && m_application->mainForm()->isActiveWindow() &&
          m_application->mainForm()->isVisible()) {
        return;
      }

      if (auto* toasts = m_application->toastNotifications(); toasts != nullptr) {
        toasts->showNotification(event, message, action);
      }
      else if (TrayIcon::isSystemTrayDesired() && m_trayIcon != nullptr && m_trayIcon->isAvailable()) {
        m_trayIcon->showMessage(message.m_title.simplified().isEmpty()
                                  ? Notification::nameForEvent(notification.event())
                                  : message.m_title,
                                message.m_message,
                                QtTrayIcon::convertIcon(message.m_type),
                                TRAY_ICON_BUBBLE_TIMEOUT,
                                action.m_action);
      }

      return;
    }
  }

  if (show_dialog && (destination.m_messageBox || message.m_type == QSystemTrayIcon::MessageIcon::Critical)) {
    MsgBox::show(parent,
                 QMessageBox::Icon(message.m_type),
                 message.m_title,
                 message.m_message,
                 {},
                 {},
                 QMessageBox::StandardButton::Ok,
                 QMessageBox::StandardButton::Ok,
                 {},
                 {MsgBox::CustomBoxAction{action.m_title, action.m_action}});
  }
  else if (destination.m_statusBar && m_application->mainForm()->statusBar() != nullptr &&
           m_application->mainForm()->statusBar()->isVisible()) {
    m_application->mainForm()->statusBar()->showMessage(message.m_message, 20000);
  }
  else {
    qDebugNN << LOGSEC_CORE << "Silencing GUI message:" << QUOTE_W_SPACE_DOT(message.m_message);
  }
}

void GuiNotificationCoordinator::showMessagesNumber(int unread_messages, bool any_feed_has_new_unread_messages) {
  Q_UNUSED(any_feed_has_new_unread_messages)

  if (m_trayIcon != nullptr) {
    m_trayIcon->setNumber(unread_messages);
  }

#if defined(Q_OS_MACOS) && QT_VERSION >= 0x060500
  m_application->setBadgeNumber(unread_messages);
#elif defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
  const bool task_bar_count_enabled =
    m_application->settings()->value(GROUP(GUI), SETTING(GUI::UnreadNumbersOnTaskBar)).toBool();
  QDBusMessage signal = QDBusMessage::createSignal(QSL("/"), QSL("com.canonical.Unity.LauncherEntry"), QSL("Update"));
  signal << QSL("application://%1.desktop").arg(APP_REVERSE_NAME);
  QVariantMap properties;
  properties.insert("count", qint64(unread_messages));
  properties.insert("count-visible", task_bar_count_enabled && unread_messages > 0);
  signal << properties;
  QDBusConnection::sessionBus().send(signal);
#elif defined(Q_OS_WIN)
  const bool task_bar_count_enabled =
    m_application->settings()->value(GROUP(GUI), SETTING(GUI::UnreadNumbersOnTaskBar)).toBool();
  auto* taskbar = m_application->windowsTaskbar();
  if (m_application->mainForm() != nullptr && taskbar != nullptr && taskbar->isAvailable()) {
    const bool paused = m_application->settings()->value(GROUP(Feeds), SETTING(Feeds::PauseFeedFetching)).toBool();
    if ((task_bar_count_enabled && unread_messages > 0) || paused) {
      taskbar->setUnreadOverlayIcon(m_application->mainForm()->winId(),
                                    unread_messages,
                                    paused && unread_messages <= 0);
    }
    else {
      taskbar->clearOverlayIcon(m_application->mainForm()->winId());
    }

    if (!task_bar_count_enabled) {
      taskbar->clearProgress(m_application->mainForm()->winId());
    }
    else if (!m_application->feedReader()->isFeedUpdateRunning()) {
      if (paused) {
        taskbar->setProgressValue(m_application->mainForm()->winId(), 100, 100);
        taskbar->setProgressState(m_application->mainForm()->winId(), WindowsTaskbar::ProgressState::Paused);
      }
      else {
        taskbar->clearProgress(m_application->mainForm()->winId());
      }
    }
  }
#endif

  if (m_application->mainForm() != nullptr) {
    m_application->mainForm()
      ->setWindowTitle((m_application->settings()->value(GROUP(GUI), SETTING(GUI::UnreadNumbersOnWindow)).toBool() &&
                        unread_messages > 0)
                         ? QSL("[%2] %1").arg(QSL(APP_LONG_NAME), QString::number(unread_messages))
                         : QSL(APP_LONG_NAME));
  }
}

void GuiNotificationCoordinator::onFeedUpdatesStarted() {
#if defined(Q_OS_WIN)
  auto* taskbar = m_application->windowsTaskbar();
  if (m_application->mainForm() != nullptr && taskbar != nullptr && taskbar->isAvailable()) {
    if (m_application->settings()->value(GROUP(GUI), SETTING(GUI::UnreadNumbersOnTaskBar)).toBool()) {
      taskbar->setProgressState(m_application->mainForm()->winId(), WindowsTaskbar::ProgressState::Indeterminate);
    }
    else {
      taskbar->clearProgress(m_application->mainForm()->winId());
    }
  }
#endif
}

void GuiNotificationCoordinator::onFeedUpdatesProgress(const Feed* feed, int current, int total) {
  Q_UNUSED(feed)
#if defined(Q_OS_WIN)
  auto* taskbar = m_application->windowsTaskbar();
  if (m_application->settings()->value(GROUP(GUI), SETTING(GUI::UnreadNumbersOnTaskBar)).toBool() &&
      m_application->mainForm() != nullptr && taskbar != nullptr && taskbar->isAvailable()) {
    if (total > 0) {
      taskbar->setProgressValue(m_application->mainForm()->winId(), current, total);
    }
    else {
      taskbar->setProgressState(m_application->mainForm()->winId(), WindowsTaskbar::ProgressState::Indeterminate);
    }
  }
#endif
}

void GuiNotificationCoordinator::onFeedUpdatesFinished(const FeedDownloadResults& results) {
  FeedDownloadResults visibleResults = results;
  QHash<Feed*, QList<Message>> automationArticles = results.updatedFeeds();
  const TorrentAutomationConfig torrentConfig = TorrentAutomationConfig::load(m_application->settings());
  if (torrentConfig.ignoreInitialFeedBatch) {
    QStringList baselined = m_application->settings()->value(QStringLiteral("TorrentAutomation"),
                                                             QStringLiteral("baselinedFeedIds")).toStringList();
    visibleResults.clear();
    visibleResults.setFeedRequestCount(results.feedRequestCount());
    for (auto it = results.erroredFeeds().constBegin(); it != results.erroredFeeds().constEnd(); ++it)
      visibleResults.appendErroredFeed(it.key(), it.value());
    for (auto it = results.updatedFeeds().constBegin(); it != results.updatedFeeds().constEnd(); ++it) {
      const QString id = it.key() == nullptr ? QString() : it.key()->customId();
      if (!id.isEmpty() && !baselined.contains(id)) {
        baselined.append(id);
        automationArticles.remove(it.key());
        continue;
      }
      visibleResults.appendUpdatedFeed(it.key(), it.value());
    }
    m_application->settings()->setValue(QStringLiteral("TorrentAutomation"),
                                        QStringLiteral("baselinedFeedIds"), baselined);
  }
  // Exclusive mode owns unattended routing for its entire lifetime, including
  // sleep periods. Normal automation must never run alongside it.
  if (torrentConfig.exclusiveModeEnabled && torrentConfig.exclusiveModeArmed)
    handleExclusiveFeedResults(results);
  else TorrentAutomationEngine::processNewArticles(automationArticles, m_application);

  const bool some_unquiet_feed = !torrentConfig.silentNotifications &&
    !qApp->property("torrentSessionSilent").toBool() &&
    qlinq::from(visibleResults.updatedFeeds().keys()).any([](Feed* feed) {
    return !feed->isQuiet();
  });

  if (some_unquiet_feed) {
    GuiMessage message = {m_application->tr("Unread articles fetched"),
                          QString(),
                          QSystemTrayIcon::MessageIcon::NoIcon};
    if (m_application->toastNotifications() != nullptr) {
      message.m_feedFetchResults = visibleResults;
    }
    else {
      message.m_message = visibleResults.overview(10);
    }
    showGuiMessage(Notification::Event::NewUnreadArticlesFetched, message, {}, {}, nullptr);
  }

#if defined(Q_OS_WIN)
  auto* taskbar = m_application->windowsTaskbar();

  if (m_application->mainForm() != nullptr && taskbar != nullptr && taskbar->isAvailable()) {
    if (results.erroredFeeds().isEmpty() ||
        !m_application->settings()->value(GROUP(GUI), SETTING(GUI::TaskbarErrorProgress)).toBool()) {
      taskbar->clearProgress(m_application->mainForm()->winId());
    }
    else {
      taskbar->setProgressValue(m_application->mainForm()->winId(), 100, 100);
      taskbar->setProgressState(m_application->mainForm()->winId(), WindowsTaskbar::ProgressState::Error);
    }
  }
#endif
}
