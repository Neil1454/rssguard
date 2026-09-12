// For license of this file, see <project-root-folder>/LICENSE.md.

#include "gui/settings/settingstorrentautomation.h"

#include "miscellaneous/application.h"
#include "miscellaneous/iconfactory.h"
#include "miscellaneous/settings.h"
#include "torrent/torrentautomationengine.h"
#include "torrent/torrentclientconfig.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QUuid>
#include <QVBoxLayout>

#include <utility>

namespace {
  constexpr double GiB = 1024.0 * 1024.0 * 1024.0;
}

SettingsTorrentAutomation::SettingsTorrentAutomation(Settings* settings, QWidget* parent)
  : SettingsPanel(settings, parent) {}

QString SettingsTorrentAutomation::title() const { return tr("Torrent automation"); }

QIcon SettingsTorrentAutomation::icon() const {
  return qApp->icons()->fromTheme(QStringLiteral("system-run"), QStringLiteral("media-playback-start"));
}

void SettingsTorrentAutomation::loadUi() {
  auto* outer = new QVBoxLayout(this);
  auto* title = new QLabel(tr("Torrent automation"), this);
  QFont font = title->font();
  font.setBold(true);
  font.setPointSize(font.pointSize() + 3);
  title->setFont(font);
  outer->addWidget(title);
  auto* description = new QLabel(tr("Automatically route new torrent RSS items to healthy clients. Start in dry-run mode and review the activity log before allowing live sends."), this);
  description->setWordWrap(true);
  outer->addWidget(description);

  auto* tabs = new QTabWidget(this);
  outer->addWidget(tabs, 1);

  auto* general = new QWidget(tabs);
  auto* generalLayout = new QVBoxLayout(general);
  auto* generalForm = new QFormLayout();
  m_enabled = new QCheckBox(tr("Enable torrent automation"), general);
  m_dryRun = new QCheckBox(tr("Dry run — report decisions without sending"), general);
  m_notifications = new QCheckBox(tr("Show automation notifications"), general);
  m_strategy = new QComboBox(general);
  for (int i = 0; i <= static_cast<int>(TorrentRoutingStrategy::Balanced); ++i)
    m_strategy->addItem(TorrentAutomationConfig::strategyName(static_cast<TorrentRoutingStrategy>(i)), i);
  m_retry = new QSpinBox(general); m_retry->setRange(1, 1440); m_retry->setSuffix(tr(" minutes"));
  m_historyLimit = new QSpinBox(general); m_historyLimit->setRange(50, 5000);
  generalForm->addRow(m_enabled);
  generalForm->addRow(m_dryRun);
  generalForm->addRow(m_notifications);
  generalForm->addRow(tr("Routing strategy:"), m_strategy);
  generalForm->addRow(tr("Retry unavailable items after:"), m_retry);
  generalForm->addRow(tr("Activity history entries:"), m_historyLimit);
  generalLayout->addLayout(generalForm);
  auto* safety = new QLabel(tr("Automation works while RSS Guard is running. Duplicate torrent URLs are recorded so a restart does not send them again."), general);
  safety->setWordWrap(true);
  generalLayout->addWidget(safety);
  generalLayout->addStretch();
  tabs->addTab(general, tr("General"));

  auto* clientsPage = new QWidget(tabs);
  auto* clientsLayout = new QVBoxLayout(clientsPage);
  auto* clientsHelp = new QLabel(tr("Limits are hard safety gates. Zero means no limit. Capacity is used only when the client's API cannot report live disk space."), clientsPage);
  clientsHelp->setWordWrap(true);
  clientsLayout->addWidget(clientsHelp);
  m_clients = new QTableWidget(clientsPage);
  m_clients->setColumnCount(8);
  m_clients->setHorizontalHeaderLabels({tr("Use"), tr("Client"), tr("Max active"), tr("Max managed"), tr("Weight"), tr("Min free GB"), tr("Capacity GB"), tr("Cleanup")});
  m_clients->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  m_clients->verticalHeader()->setVisible(false);
  clientsLayout->addWidget(m_clients);
  tabs->addTab(clientsPage, tr("Clients and limits"));

  auto* rulesPage = new QWidget(tabs);
  auto* rulesLayout = new QVBoxLayout(rulesPage);
  auto* rulesHelp = new QLabel(tr("Rules are checked from top to bottom. With no rules, every new RSS item containing a torrent link is eligible."), rulesPage);
  rulesHelp->setWordWrap(true);
  rulesLayout->addWidget(rulesHelp);
  m_rules = new QListWidget(rulesPage);
  rulesLayout->addWidget(m_rules, 1);
  auto* ruleButtons = new QHBoxLayout();
  auto* add = new QPushButton(tr("Add rule"), rulesPage);
  m_editRule = new QPushButton(tr("Edit"), rulesPage);
  m_removeRule = new QPushButton(tr("Remove"), rulesPage);
  ruleButtons->addWidget(add); ruleButtons->addWidget(m_editRule); ruleButtons->addWidget(m_removeRule); ruleButtons->addStretch();
  rulesLayout->addLayout(ruleButtons);
  tabs->addTab(rulesPage, tr("RSS rules"));

  auto* cleanupPage = new QWidget(tabs);
  auto* cleanupLayout = new QVBoxLayout(cleanupPage);
  auto* warning = new QLabel(tr("Cleanup is destructive. It only considers completed torrents carrying RSS Guard's automation marker; manually added torrents are never eligible."), cleanupPage);
  warning->setWordWrap(true);
  cleanupLayout->addWidget(warning);
  auto* cleanupForm = new QFormLayout();
  m_cleanup = new QCheckBox(tr("Allow automatic cleanup when a client is below its free-space limit"), cleanupPage);
  m_deleteData = new QCheckBox(tr("Delete downloaded data as well as the torrent"), cleanupPage);
  m_confirmCleanup = new QCheckBox(tr("Ask before every removal"), cleanupPage);
  m_seedHours = new QSpinBox(cleanupPage); m_seedHours->setRange(0, 100000); m_seedHours->setSuffix(tr(" hours"));
  m_ratio = new QDoubleSpinBox(cleanupPage); m_ratio->setRange(0, 10000); m_ratio->setDecimals(2);
  m_inactiveHours = new QSpinBox(cleanupPage); m_inactiveHours->setRange(0, 100000); m_inactiveHours->setSuffix(tr(" hours"));
  m_maxRemovals = new QSpinBox(cleanupPage); m_maxRemovals->setRange(1, 100);
  m_cleanupStopGb = new QDoubleSpinBox(cleanupPage); m_cleanupStopGb->setRange(0, 1000000); m_cleanupStopGb->setSuffix(tr(" GB"));
  cleanupForm->addRow(m_cleanup);
  cleanupForm->addRow(m_deleteData);
  cleanupForm->addRow(m_confirmCleanup);
  cleanupForm->addRow(tr("Minimum completed/seeding age:"), m_seedHours);
  cleanupForm->addRow(tr("Minimum ratio:"), m_ratio);
  cleanupForm->addRow(tr("Minimum inactivity:"), m_inactiveHours);
  cleanupForm->addRow(tr("Maximum removals per run:"), m_maxRemovals);
  cleanupForm->addRow(tr("Target free space after cleanup:"), m_cleanupStopGb);
  cleanupLayout->addLayout(cleanupForm);
  cleanupLayout->addStretch();
  tabs->addTab(cleanupPage, tr("Safe cleanup"));

  auto* activityPage = new QWidget(tabs);
  auto* activityLayout = new QVBoxLayout(activityPage);
  m_activity = new QListWidget(activityPage);
  auto* refresh = new QPushButton(tr("Refresh activity"), activityPage);
  activityLayout->addWidget(m_activity, 1);
  activityLayout->addWidget(refresh, 0, Qt::AlignRight);
  tabs->addTab(activityPage, tr("Activity"));

  const QList<QObject*> dirtyObjects{m_enabled, m_dryRun, m_notifications, m_strategy, m_retry, m_historyLimit,
                                     m_cleanup, m_deleteData, m_confirmCleanup, m_seedHours, m_ratio,
                                     m_inactiveHours, m_maxRemovals, m_cleanupStopGb, m_clients};
  for (QObject* object : dirtyObjects) {
    if (auto* box = qobject_cast<QCheckBox*>(object)) connect(box, &QCheckBox::toggled, this, &SettingsTorrentAutomation::dirtifySettings);
    else if (auto* combo = qobject_cast<QComboBox*>(object)) connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsTorrentAutomation::dirtifySettings);
    else if (auto* spin = qobject_cast<QSpinBox*>(object)) connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsTorrentAutomation::dirtifySettings);
    else if (auto* dspin = qobject_cast<QDoubleSpinBox*>(object)) connect(dspin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SettingsTorrentAutomation::dirtifySettings);
  }
  connect(m_clients, &QTableWidget::cellChanged, this, &SettingsTorrentAutomation::dirtifySettings);
  connect(add, &QPushButton::clicked, this, &SettingsTorrentAutomation::addRule);
  connect(m_editRule, &QPushButton::clicked, this, &SettingsTorrentAutomation::editRule);
  connect(m_removeRule, &QPushButton::clicked, this, &SettingsTorrentAutomation::removeRule);
  connect(m_rules, &QListWidget::itemDoubleClicked, this, [this]() { editRule(); });
  connect(m_rules, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
    const int row = m_rules->row(item);
    if (row >= 0 && row < m_config.rules.size()) {
      m_config.rules[row].enabled = item->checkState() == Qt::Checked;
      dirtifySettings();
    }
  });
  connect(m_rules, &QListWidget::currentRowChanged, this, [this](int row) { m_editRule->setEnabled(row >= 0); m_removeRule->setEnabled(row >= 0); });
  connect(refresh, &QPushButton::clicked, this, &SettingsTorrentAutomation::refreshActivity);
  connect(m_cleanup, &QCheckBox::toggled, this, &SettingsTorrentAutomation::updateCleanupControls);
  SettingsPanel::loadUi();
}

void SettingsTorrentAutomation::loadSettings() {
  onBeginLoadSettings();
  m_config = TorrentAutomationConfig::load(settings());
  m_enabled->setChecked(m_config.enabled);
  m_dryRun->setChecked(m_config.dryRun);
  m_notifications->setChecked(m_config.showNotifications);
  m_strategy->setCurrentIndex(m_strategy->findData(static_cast<int>(m_config.strategy)));
  m_retry->setValue(m_config.retryMinutes);
  m_historyLimit->setValue(m_config.historyLimit);
  m_cleanup->setChecked(m_config.cleanupEnabled);
  m_deleteData->setChecked(m_config.deleteData);
  m_confirmCleanup->setChecked(m_config.cleanupRequireConfirmation);
  m_seedHours->setValue(m_config.minimumSeedHours);
  m_ratio->setValue(m_config.minimumRatio);
  m_inactiveHours->setValue(m_config.minimumInactiveHours);
  m_maxRemovals->setValue(m_config.maximumRemovalsPerRun);
  m_cleanupStopGb->setValue(m_config.cleanupStopFreeBytes / GiB);
  refreshClientPolicies();
  refreshRules();
  refreshActivity();
  updateCleanupControls();
  onEndLoadSettings();
}

void SettingsTorrentAutomation::saveSettings() {
  onBeginSaveSettings();
  m_config.enabled = m_enabled->isChecked();
  m_config.dryRun = m_dryRun->isChecked();
  m_config.showNotifications = m_notifications->isChecked();
  m_config.strategy = static_cast<TorrentRoutingStrategy>(m_strategy->currentData().toInt());
  m_config.retryMinutes = m_retry->value();
  m_config.historyLimit = m_historyLimit->value();
  m_config.cleanupEnabled = m_cleanup->isChecked();
  m_config.deleteData = m_deleteData->isChecked();
  m_config.cleanupRequireConfirmation = m_confirmCleanup->isChecked();
  m_config.minimumSeedHours = m_seedHours->value();
  m_config.minimumRatio = m_ratio->value();
  m_config.minimumInactiveHours = m_inactiveHours->value();
  m_config.maximumRemovalsPerRun = m_maxRemovals->value();
  m_config.cleanupStopFreeBytes = qint64(m_cleanupStopGb->value() * GiB);
  m_config.clients.clear();
  for (int row = 0; row < m_clients->rowCount(); ++row) {
    TorrentAutomationClientPolicy policy;
    policy.clientId = m_clients->item(row, 1)->data(Qt::UserRole).toString();
    policy.enabled = m_clients->item(row, 0)->checkState() == Qt::Checked;
    policy.maxActiveDownloads = m_clients->item(row, 2)->text().toInt();
    policy.maxManagedTorrents = m_clients->item(row, 3)->text().toInt();
    policy.weight = qMax(1, m_clients->item(row, 4)->text().toInt());
    policy.minimumFreeBytes = qint64(m_clients->item(row, 5)->text().toDouble() * GiB);
    policy.configuredCapacityBytes = qint64(m_clients->item(row, 6)->text().toDouble() * GiB);
    policy.allowCleanup = m_clients->item(row, 7)->checkState() == Qt::Checked;
    m_config.clients.append(policy);
  }
  m_config.save(settings());
  onEndSaveSettings();
}

void SettingsTorrentAutomation::refreshClientPolicies() {
  const QList<TorrentClientConfig> clients = TorrentClientConfig::enabledInPriorityOrder(TorrentClientConfig::load(settings()));
  m_clients->blockSignals(true);
  m_clients->setRowCount(clients.size());
  for (int row = 0; row < clients.size(); ++row) {
    const TorrentClientConfig& client = clients.at(row);
    const TorrentAutomationClientPolicy policy = m_config.policyFor(client.id);
    auto* use = new QTableWidgetItem(); use->setCheckState(policy.enabled ? Qt::Checked : Qt::Unchecked);
    auto* name = new QTableWidgetItem(client.name); name->setData(Qt::UserRole, client.id); name->setFlags(name->flags() & ~Qt::ItemIsEditable);
    auto* cleanup = new QTableWidgetItem();
    const bool cleanupSupported = client.type == TorrentClientType::QBittorrent || client.type == TorrentClientType::Transmission;
    cleanup->setCheckState(cleanupSupported && policy.allowCleanup ? Qt::Checked : Qt::Unchecked);
    if (!cleanupSupported) {
      cleanup->setFlags(cleanup->flags() & ~Qt::ItemIsEnabled);
      cleanup->setToolTip(tr("Disabled because this adapter cannot yet identify RSS Guard-managed torrents safely."));
    }
    m_clients->setItem(row, 0, use); m_clients->setItem(row, 1, name);
    m_clients->setItem(row, 2, new QTableWidgetItem(QString::number(policy.maxActiveDownloads)));
    m_clients->setItem(row, 3, new QTableWidgetItem(QString::number(policy.maxManagedTorrents)));
    m_clients->setItem(row, 4, new QTableWidgetItem(QString::number(policy.weight)));
    m_clients->setItem(row, 5, new QTableWidgetItem(QString::number(policy.minimumFreeBytes / GiB, 'f', 1)));
    m_clients->setItem(row, 6, new QTableWidgetItem(QString::number(policy.configuredCapacityBytes / GiB, 'f', 1)));
    m_clients->setItem(row, 7, cleanup);
  }
  m_clients->blockSignals(false);
}

void SettingsTorrentAutomation::refreshRules() {
  m_rules->blockSignals(true);
  m_rules->clear();
  for (const TorrentAutomationRule& rule : std::as_const(m_config.rules)) {
    auto* item = new QListWidgetItem(rule.name.isEmpty() ? tr("Unnamed rule") : rule.name, m_rules);
    item->setCheckState(rule.enabled ? Qt::Checked : Qt::Unchecked);
    item->setToolTip(rule.titleRegularExpression.isEmpty() ? tr("All matching feed items") : rule.titleRegularExpression);
  }
  m_editRule->setEnabled(m_rules->currentRow() >= 0);
  m_removeRule->setEnabled(m_rules->currentRow() >= 0);
  m_rules->blockSignals(false);
}

TorrentAutomationRule SettingsTorrentAutomation::editRuleDialog(const TorrentAutomationRule& initial, bool* accepted) {
  QDialog dialog(this);
  dialog.setWindowTitle(initial.id.isEmpty() ? tr("Add automation rule") : tr("Edit automation rule"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  auto* enabled = new QCheckBox(tr("Enabled"), &dialog); enabled->setChecked(initial.enabled);
  auto* name = new QLineEdit(initial.name, &dialog);
  auto* feeds = new QLineEdit(initial.feedIds.join(QStringLiteral(", ")), &dialog);
  feeds->setPlaceholderText(tr("Leave empty for all feeds; otherwise enter feed IDs separated by commas"));
  auto* required = new QLineEdit(initial.requiredText, &dialog);
  auto* excluded = new QLineEdit(initial.excludedText, &dialog);
  auto* regex = new QLineEdit(initial.titleRegularExpression, &dialog);
  regex->setPlaceholderText(tr("Optional regular expression matched against the article title"));
  auto* clientList = new QListWidget(&dialog);
  for (const TorrentClientConfig& client : TorrentClientConfig::enabledInPriorityOrder(TorrentClientConfig::load(settings()))) {
    auto* item = new QListWidgetItem(client.name, clientList);
    item->setData(Qt::UserRole, client.id);
    item->setCheckState(initial.clientIds.isEmpty() || initial.clientIds.contains(client.id) ? Qt::Checked : Qt::Unchecked);
  }
  form->addRow(enabled); form->addRow(tr("Rule name:"), name); form->addRow(tr("Feed IDs:"), feeds);
  form->addRow(tr("Title/content must contain:"), required); form->addRow(tr("Must not contain:"), excluded);
  form->addRow(tr("Title pattern:"), regex); form->addRow(tr("Allowed clients:"), clientList);
  layout->addLayout(form);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  *accepted = dialog.exec() == QDialog::Accepted;
  if (!*accepted) return initial;
  TorrentAutomationRule result = initial;
  if (result.id.isEmpty()) result.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  result.enabled = enabled->isChecked(); result.name = name->text().trimmed();
  result.feedIds = feeds->text().split(QLatin1Char(','), Qt::SkipEmptyParts);
  for (QString& feed : result.feedIds) feed = feed.trimmed();
  result.requiredText = required->text().trimmed(); result.excludedText = excluded->text().trimmed();
  result.titleRegularExpression = regex->text().trimmed(); result.clientIds.clear();
  for (int i = 0; i < clientList->count(); ++i) if (clientList->item(i)->checkState() == Qt::Checked)
    result.clientIds.append(clientList->item(i)->data(Qt::UserRole).toString());
  return result;
}

void SettingsTorrentAutomation::addRule() {
  bool accepted = false;
  TorrentAutomationRule rule; rule.enabled = true;
  rule = editRuleDialog(rule, &accepted);
  if (!accepted) return;
  m_config.rules.append(rule); refreshRules(); dirtifySettings();
}

void SettingsTorrentAutomation::editRule() {
  const int row = m_rules->currentRow(); if (row < 0) return;
  bool accepted = false;
  const TorrentAutomationRule rule = editRuleDialog(m_config.rules.at(row), &accepted);
  if (!accepted) return;
  m_config.rules[row] = rule; refreshRules(); m_rules->setCurrentRow(row); dirtifySettings();
}

void SettingsTorrentAutomation::removeRule() {
  const int row = m_rules->currentRow(); if (row < 0) return;
  if (QMessageBox::question(this, tr("Remove automation rule"), tr("Remove “%1”?").arg(m_config.rules.at(row).name)) != QMessageBox::Yes) return;
  m_config.rules.removeAt(row); refreshRules(); dirtifySettings();
}

void SettingsTorrentAutomation::refreshActivity() {
  m_activity->clear();
  m_activity->addItems(TorrentAutomationEngine::instance(qApp)->recentActivity());
  if (m_activity->count() == 0) m_activity->addItem(tr("No automation activity has been recorded yet."));
}

void SettingsTorrentAutomation::updateCleanupControls() {
  const bool enabled = m_cleanup->isChecked();
  for (QWidget* widget : {static_cast<QWidget*>(m_deleteData), m_confirmCleanup, m_seedHours, m_ratio,
                          m_inactiveHours, m_maxRemovals, m_cleanupStopGb}) widget->setEnabled(enabled);
}
