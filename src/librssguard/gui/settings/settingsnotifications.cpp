// For license of this file, see <project-root-folder>/LICENSE.md.

#include "gui/settings/settingsnotifications.h"

#include "definitions/globals.h"
#include "gui/notifications/notificationseditor.h"
#include "gui/notifications/toastnotificationsmanager.h"
#include "miscellaneous/application.h"
#include "miscellaneous/iconfactory.h"
#include "miscellaneous/notificationfactory.h"
#include "miscellaneous/settings.h"
#include "miscellaneous/settingskeys.h"

#include <QDir>
#include <QCheckBox>
#include <QScreen>

SettingsNotifications::SettingsNotifications(Settings* settings, QWidget* parent)
  : SettingsPanel(settings, parent), m_ui(nullptr) {}

SettingsNotifications::~SettingsNotifications() {
  if (m_ui != nullptr) {
    delete m_ui;
  }
}

void SettingsNotifications::loadUi() {
  m_ui = new Ui::SettingsNotifications();
  m_ui->setupUi(this);

  m_keepArticleNotificationsOpen = new QCheckBox(tr("Keep new-article notifications open until dismissed"), this);
  m_keepArticleNotificationsOpen
    ->setToolTip(tr("Disables the timeout and right-click dismissal for new-article notifications. Use the close button to dismiss them."));
  m_ui->formLayout_3->insertRow(5, m_keepArticleNotificationsOpen);
  m_previewTorrentButtons = new QCheckBox(tr("Include torrent-client buttons in notification preview"), this);
  m_previewTorrentButtons->setToolTip(tr("The test notification uses the real new-article layout and shows enabled clients in priority order."));
  m_ui->formLayout_3->insertRow(6, m_previewTorrentButtons);

  m_ui->m_lblInfo
    ->setHelpText(tr("There are some built-in notification sounds. Just start typing \":\" and they will show up."),
                  true);

  m_ui->m_lblNativeNotif->setHelpText(tr("Note that native notifications might have some OS-dependent limitations. For "
                                         "example Windows OS is known to limit the amount of notification originating "
                                         "from each app during short span of time."),
                                      true);

  connect(m_ui->m_checkEnableNotifications, &QCheckBox::toggled, this, &SettingsNotifications::dirtifySettings);
  connect(m_ui->m_editor, &NotificationsEditor::someNotificationChanged, this, &SettingsNotifications::dirtifySettings);

  connect(m_ui->m_rbCustomNotifications, &QRadioButton::toggled, this, &SettingsNotifications::dirtifySettings);
  connect(m_ui->m_rbCustomNotifications, &QRadioButton::toggled, this, &SettingsNotifications::requireRestart);

  connect(m_ui->m_rbNativeNotifications, &QRadioButton::toggled, this, &SettingsNotifications::dirtifySettings);
  connect(m_ui->m_rbNativeNotifications, &QRadioButton::toggled, this, &SettingsNotifications::requireRestart);

  connect(m_ui->m_sbDuration,
          QOverload<int>::of(&QSpinBox::valueChanged),
          this,
          &SettingsNotifications::dirtifySettings);
  connect(m_keepArticleNotificationsOpen,
          &QCheckBox::toggled,
          this,
          &SettingsNotifications::dirtifySettings);
  connect(m_previewTorrentButtons, &QCheckBox::toggled, this, &SettingsNotifications::dirtifySettings);
  connect(m_ui->m_sbScreen, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsNotifications::dirtifySettings);
  connect(m_ui->m_sbMargin, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsNotifications::dirtifySettings);
  connect(m_ui->m_sbWidth, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsNotifications::dirtifySettings);
  connect(m_ui->m_sbOpacity,
          QOverload<int>::of(&QSpinBox::valueChanged),
          this,
          &SettingsNotifications::dirtifySettings);

  connect(m_ui->m_sbScreen, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsNotifications::showScreenInfo);

  connect(m_ui->m_cbCustomNotificationsPosition,
          QOverload<int>::of(&QComboBox::currentIndexChanged),
          this,
          &SettingsNotifications::dirtifySettings);

  SettingsPanel::loadUi();
}

QIcon SettingsNotifications::icon() const {
  return qApp->icons()->fromTheme(QSL("notifications"), QSL("dialog-information"));
}

void SettingsNotifications::loadSettings() {
  onBeginLoadSettings();

  m_ui->m_sbScreen->setMinimum(-1);
  m_ui->m_sbScreen->setMaximum(QGuiApplication::screens().size() - 1);

  auto poss = enumToStrings<ToastNotificationsManager::NotificationPosition>();

  for (const auto& pos : std::as_const(poss)) {
    m_ui->m_cbCustomNotificationsPosition->addItem(ToastNotificationsManager::textForPosition(pos.first), pos.first);
  }

  // Load fancy notification settings.
  m_ui->m_checkEnableNotifications
    ->setChecked(settings()->value(GROUP(GUI), SETTING(GUI::EnableNotifications)).toBool());
  m_ui->m_editor->loadNotifications(qApp->notifications()->allNotifications());

  if (qApp->isWayland()) {
    // Wayland does not support fancy notifications, only system ones.
    m_ui->m_rbNativeNotifications->setChecked(true);
    m_ui->m_rbCustomNotifications->setEnabled(false);
    m_ui->m_rbCustomNotifications
      ->setText(tr("%1 (not supported on Wayland)").arg(m_ui->m_rbCustomNotifications->text()));
  }
  else {
    m_ui->m_rbNativeNotifications
      ->setChecked(!settings()->value(GROUP(GUI), SETTING(GUI::UseToastNotifications)).toBool());
  }

  m_ui->m_sbDuration->setValue(settings()->value(GROUP(GUI), SETTING(GUI::ToastNotificationsDuration)).toInt());
  m_keepArticleNotificationsOpen
    ->setChecked(settings()->value(GROUP(GUI), SETTING(GUI::KeepArticleNotificationsOpen)).toBool());
  m_previewTorrentButtons->setChecked(settings()->value(QStringLiteral("NotificationsPreview"),
                                                         QStringLiteral("includeTorrentButtons"), true).toBool());
  m_ui->m_sbScreen->setValue(settings()->value(GROUP(GUI), SETTING(GUI::ToastNotificationsScreen)).toInt());
  m_ui->m_sbWidth->setValue(settings()->value(GROUP(GUI), SETTING(GUI::ToastNotificationsWidth)).toInt());
  m_ui->m_sbMargin->setValue(settings()->value(GROUP(GUI), SETTING(GUI::ToastNotificationsMargin)).toInt());
  m_ui->m_sbOpacity->setValue(settings()->value(GROUP(GUI), SETTING(GUI::ToastNotificationsOpacity)).toDouble() * 100);

  m_ui->m_cbCustomNotificationsPosition
    ->setCurrentIndex(m_ui->m_cbCustomNotificationsPosition
                        ->findData(settings()
                                     ->value(GROUP(GUI), SETTING(GUI::ToastNotificationsPosition))
                                     .value<ToastNotificationsManager::NotificationPosition>()));

  onEndLoadSettings();
}

void SettingsNotifications::saveSettings() {
  onBeginSaveSettings();

  // Save notifications.
  settings()->setValue(GROUP(GUI), GUI::EnableNotifications, m_ui->m_checkEnableNotifications->isChecked());
  qApp->notifications()->save(m_ui->m_editor->allNotifications(), settings());

  settings()->setValue(GROUP(GUI), GUI::UseToastNotifications, m_ui->m_rbCustomNotifications->isChecked());
  settings()->setValue(GROUP(GUI), GUI::ToastNotificationsDuration, m_ui->m_sbDuration->value());
  settings()->setValue(GROUP(GUI),
                       GUI::KeepArticleNotificationsOpen,
                       m_keepArticleNotificationsOpen->isChecked());
  settings()->setValue(QStringLiteral("NotificationsPreview"),
                       QStringLiteral("includeTorrentButtons"),
                       m_previewTorrentButtons->isChecked());
  settings()->setValue(GROUP(GUI), GUI::ToastNotificationsScreen, m_ui->m_sbScreen->value());
  settings()->setValue(GROUP(GUI), GUI::ToastNotificationsWidth, m_ui->m_sbWidth->value());
  settings()->setValue(GROUP(GUI), GUI::ToastNotificationsMargin, m_ui->m_sbMargin->value());
  settings()->setValue(GROUP(GUI), GUI::ToastNotificationsOpacity, m_ui->m_sbOpacity->value() / 100.0);

  settings()->setValue(GROUP(GUI),
                       GUI::ToastNotificationsPosition,
                       m_ui->m_cbCustomNotificationsPosition->currentData()
                         .value<ToastNotificationsManager::NotificationPosition>());

  auto* toasts = qApp->toastNotifications();

  if (toasts != nullptr && m_ui->m_rbCustomNotifications->isChecked() &&
      m_ui->m_checkEnableNotifications->isChecked()) {
    toasts->resetNotifications(true);
    toasts->showArticleListPreview(m_previewTorrentButtons->isChecked());
  }

  onEndSaveSettings();
}

void SettingsNotifications::showScreenInfo(int index) {
  QScreen* scr;

  if (index < 0 || index >= QGuiApplication::screens().size()) {
    scr = QGuiApplication::primaryScreen();
  }
  else {
    scr = QGuiApplication::screens().at(index);
  }

  m_ui->m_lblScreenInfo->setText(QSL("%1 (%2x%3)")
                                   .arg(scr->name(),
                                        QString::number(scr->virtualSize().width()),
                                        QString::number(scr->virtualSize().height())));
}
