// For license of this file, see <project-root-folder>/LICENSE.md.

#include "gui/settings/settingstorrentautomation.h"

#include "core/feedsmodel.h"
#include "miscellaneous/application.h"
#include "miscellaneous/feedreader.h"
#include "miscellaneous/iconfactory.h"
#include "miscellaneous/settings.h"
#include "services/abstract/feed.h"
#include "torrent/torrentautomationengine.h"
#include "torrent/torrentclient.h"
#include "torrent/torrentclientconfig.h"

#include <QCheckBox>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QSaveFile>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

#include <utility>
#include <memory>

namespace {
  constexpr double GiB = 1024.0 * 1024.0 * 1024.0;

  QString capabilitySummary(const TorrentClientConfig& config,
                            const TorrentClientStatus& status,
                            bool transferRatesAvailable,
                            bool removalAvailable) {
    QStringList parts;
    parts << (status.reachable ? QObject::tr("connection and workload verified")
                               : QObject::tr("status request failed"));
    if (status.liveSpace) {
      parts << (status.totalBytes > 0
                  ? QObject::tr("live free space and total capacity reported")
                  : QObject::tr("live free space reported; total capacity unavailable"));
    }
    else if (config.type == TorrentClientType::RTorrent || config.type == TorrentClientType::RQBit) {
      parts << QObject::tr("disk space is not exposed by this portable API; configured capacity is required");
    }
    else parts << QObject::tr("disk-space request was unavailable");
    parts << (transferRatesAvailable ? QObject::tr("transfer-rate values reported")
                                     : QObject::tr("transfer-rate values were not reported"));
    parts << (removalAvailable ? QObject::tr("removal API supported by the adapter; server permission not destructively tested")
                               : QObject::tr("removal API unavailable"));
    if (!status.detail.isEmpty()) parts << status.detail;
    return parts.join(QStringLiteral("; "));
  }

  bool hasTransferRateValues(const TorrentClientStatus& status) {
    if (status.downloadBytesPerSecond >= 0 || status.uploadBytesPerSecond >= 0) return true;
    return std::any_of(status.torrents.cbegin(), status.torrents.cend(), [](const TorrentRemoteItem& item) {
      return item.downloadBytesPerSecond >= 0 || item.uploadBytesPerSecond >= 0;
    });
  }

  bool transientStatusFailure(const QString& message) {
    const QString lower = message.toLower();
    const QStringList markers{QStringLiteral("timeout"), QStringLiteral("timed out"),
                              QStringLiteral("temporarily"), QStringLiteral("unavailable"),
                              QStringLiteral("connection refused"), QStringLiteral("connection reset"),
                              QStringLiteral("host not found"), QStringLiteral("http 429"),
                              QStringLiteral("http 502"), QStringLiteral("http 503"),
                              QStringLiteral("http 504")};
    return std::any_of(markers.cbegin(), markers.cend(), [&lower](const QString& marker) {
      return lower.contains(marker);
    });
  }
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
  const QStringList strategyTips{
    tr("Always choose the eligible client with the lowest automation-priority number."),
    tr("Choose the eligible client reporting the fewest active downloads."),
    tr("Choose the eligible client reporting the most free disk space."),
    tr("Rotate evenly through eligible clients."),
    tr("Distribute across eligible clients while favouring lower automation-priority numbers."),
    tr("Combine free-space ratio, active and queued downloads, and automation priority.")};
  for (int index = 0; index < strategyTips.size(); ++index)
    m_strategy->setItemData(index, strategyTips.at(index), Qt::ToolTipRole);
  m_historyLimit = new QSpinBox(general); m_historyLimit->setRange(50, 5000);
  m_unknownSizeGb = new QDoubleSpinBox(general); m_unknownSizeGb->setRange(0.1, 1000000.0);
  m_unknownSizeGb->setDecimals(1); m_unknownSizeGb->setSuffix(tr(" GB"));
  m_enabled->setToolTip(tr("Master switch. When off, new RSS items are never routed automatically."));
  m_dryRun->setToolTip(tr("Safely exercise rules and routing without sending or deleting anything. Decisions are written to Activity."));
  m_notifications->setToolTip(tr("Show a notification when automation sends, holds, retries, cleans up, or fails an item."));
  m_strategy->setToolTip(tr("Chooses which eligible client receives a torrent. Limits and RSS rules are checked before this strategy is used."));
  m_historyLimit->setToolTip(tr("Maximum number of automation events retained. Oldest entries are removed first."));
  m_unknownSizeGb->setToolTip(tr("Space reserved when an RSS torrent link does not declare its size. Magnet links containing an exact xl value use that value instead."));
  generalForm->addRow(m_enabled);
  generalForm->addRow(m_dryRun);
  generalForm->addRow(m_notifications);
  generalForm->addRow(tr("Routing strategy:"), m_strategy);
  generalForm->addRow(tr("Activity history entries:"), m_historyLimit);
  generalForm->addRow(tr("Assumed size when unknown:"), m_unknownSizeGb);
  generalLayout->addLayout(generalForm);
  m_runDryTest = new QPushButton(tr("Run dry test now"), general);
  auto* readinessAudit = new QPushButton(tr("Check readiness for live automation"), general);
  m_runDryTest->setToolTip(tr("Re-evaluate the most recently fetched RSS items using live client status. Nothing is sent, removed or deleted."));
  readinessAudit->setToolTip(tr("Inspect client tests, storage information, rules, retry protection and cleanup safeguards before disabling Dry run."));
  auto* validationButtons = new QHBoxLayout(); validationButtons->addStretch();
  validationButtons->addWidget(readinessAudit); validationButtons->addWidget(m_runDryTest);
  generalLayout->addLayout(validationButtons);
  auto* configurationButtons = new QHBoxLayout();
  auto* exportConfiguration = new QPushButton(tr("Export torrent configuration"), general);
  auto* importConfiguration = new QPushButton(tr("Import torrent configuration"), general);
  exportConfiguration->setToolTip(tr("Save torrent clients and automation rules to a portable JSON file. Passwords and API tokens are never exported."));
  importConfiguration->setToolTip(tr("Import a previously exported torrent configuration. Existing matching credentials are retained; credentials are never read from the file."));
  configurationButtons->addStretch();
  configurationButtons->addWidget(exportConfiguration);
  configurationButtons->addWidget(importConfiguration);
  generalLayout->addLayout(configurationButtons);
  auto* safety = new QLabel(tr("Automation works while RSS Guard is running. Duplicate torrent URLs are recorded so a restart does not send them again."), general);
  safety->setWordWrap(true);
  generalLayout->addWidget(safety);
  generalLayout->addStretch();
  const int generalTab = tabs->addTab(general, tr("General"));
  tabs->setTabToolTip(generalTab, tr("Turn automation on, select its routing method, and configure history and unknown-size reservations."));

  auto* retriesPage = new QWidget(tabs);
  auto* retriesLayout = new QVBoxLayout(retriesPage);
  auto* retriesForm = new QFormLayout();
  m_retryEnabled = new QCheckBox(tr("Retry temporary connection and capacity failures"), retriesPage);
  m_retryAttempts = new QSpinBox(retriesPage); m_retryAttempts->setRange(0, 20);
  m_retryInitialSeconds = new QSpinBox(retriesPage); m_retryInitialSeconds->setRange(1, 86400); m_retryInitialSeconds->setSuffix(tr(" seconds"));
  m_retryMaximumSeconds = new QSpinBox(retriesPage); m_retryMaximumSeconds->setRange(1, 86400); m_retryMaximumSeconds->setSuffix(tr(" seconds"));
  m_retryBackoff = new QCheckBox(tr("Increase the delay after each failed attempt"), retriesPage);
  m_requestTimeoutSeconds = new QSpinBox(retriesPage); m_requestTimeoutSeconds->setRange(5, 300); m_requestTimeoutSeconds->setSuffix(tr(" seconds"));
  m_retryEnabled->setToolTip(tr("Retry temporary timeouts, connection failures and unavailable-capacity decisions. Authentication and configuration errors are not retried."));
  m_retryAttempts->setToolTip(tr("Maximum retry attempts after the first attempt. Per-client values in Clients and limits can override this."));
  m_retryInitialSeconds->setToolTip(tr("Delay before the first retry. With increasing delay enabled, later retries wait progressively longer."));
  m_retryMaximumSeconds->setToolTip(tr("Longest delay permitted between retry attempts."));
  m_retryBackoff->setToolTip(tr("Use exponential backoff so a struggling seedbox is not repeatedly contacted under heavy load."));
  m_requestTimeoutSeconds->setToolTip(tr("Default time allowed for a torrent-client request before that client is treated as temporarily unavailable."));
  retriesForm->addRow(m_retryEnabled);
  retriesForm->addRow(tr("Retries after first attempt:"), m_retryAttempts);
  retriesForm->addRow(tr("Initial retry delay:"), m_retryInitialSeconds);
  retriesForm->addRow(tr("Maximum retry delay:"), m_retryMaximumSeconds);
  retriesForm->addRow(m_retryBackoff);
  retriesForm->addRow(tr("Default request timeout:"), m_requestTimeoutSeconds);
  retriesLayout->addLayout(retriesForm);
  auto* retryNote = new QLabel(tr("If a preferred client is unavailable, automatic routing immediately considers the next healthy client. Items are retained for later retry when no safe destination is available."), retriesPage);
  retryNote->setWordWrap(true); retriesLayout->addWidget(retryNote); retriesLayout->addStretch();
  const int retriesTab = tabs->addTab(retriesPage, tr("Retries and health"));
  tabs->setTabToolTip(retriesTab, tr("Control request timeouts, retry limits and backoff for temporarily unavailable torrent clients."));

  auto* maintenancePage = new QWidget(tabs);
  auto* maintenanceLayout = new QVBoxLayout(maintenancePage);
  auto* maintenanceForm = new QFormLayout();
  m_reconciliation = new QCheckBox(tr("Reconcile managed storage with live torrent lists"), maintenancePage);
  m_reserveRemaining = new QCheckBox(tr("Reserve space still needed by unfinished managed downloads"), maintenancePage);
  m_preventDuplicates = new QCheckBox(tr("Skip automatic sends when the torrent already exists"), maintenancePage);
  m_reconciliationMinutes = new QSpinBox(maintenancePage); m_reconciliationMinutes->setRange(1, 1440); m_reconciliationMinutes->setSuffix(tr(" minutes"));
  m_circuitBreaker = new QCheckBox(tr("Temporarily sideline repeatedly failing clients"), maintenancePage);
  m_breakerFailures = new QSpinBox(maintenancePage); m_breakerFailures->setRange(1, 20);
  m_breakerCooldown = new QSpinBox(maintenancePage); m_breakerCooldown->setRange(1, 1440); m_breakerCooldown->setSuffix(tr(" minutes"));
  m_breakerRecoverySuccesses = new QSpinBox(maintenancePage); m_breakerRecoverySuccesses->setRange(1, 10);
  m_schedule = new QCheckBox(tr("Limit unattended routing to these hours"), maintenancePage);
  m_scheduleStart = new QComboBox(maintenancePage); m_scheduleEnd = new QComboBox(maintenancePage);
  for (int hour = 0; hour <= 24; ++hour) {
    const QString label = QStringLiteral("%1:00").arg(hour, 2, 10, QLatin1Char('0'));
    if (hour < 24) m_scheduleStart->addItem(label, hour);
    m_scheduleEnd->addItem(label, hour);
  }
  auto* scheduleRow = new QWidget(maintenancePage); auto* scheduleLayout = new QHBoxLayout(scheduleRow);
  scheduleLayout->setContentsMargins(0, 0, 0, 0); scheduleLayout->addWidget(m_scheduleStart); scheduleLayout->addWidget(new QLabel(tr("to"), scheduleRow)); scheduleLayout->addWidget(m_scheduleEnd);
  m_reconciliation->setToolTip(tr("After live status is read, repair managed sizes, progress reservations, removed entries and RSS Guard-owned entries found on the server."));
  m_reserveRemaining->setToolTip(tr("Keep unfinished bytes reserved when deciding whether another torrent safely fits. This reduces overfilling while several downloads are active."));
  m_preventDuplicates->setToolTip(tr("Compare magnet info hashes across reachable clients before unattended sending. Direct named-client actions can still intentionally create another copy."));
  m_reconciliationMinutes->setToolTip(tr("Minimum intended interval between full reconciliation passes. Normal routing still obtains current workload status."));
  m_circuitBreaker->setToolTip(tr("After repeated failures, stop contacting that client for the cooldown period while other clients continue normally."));
  m_breakerFailures->setToolTip(tr("Consecutive failed status cycles before the client is temporarily sidelined."));
  m_breakerCooldown->setToolTip(tr("How long a sidelined client remains out of automatic checks before a recovery attempt."));
  m_breakerRecoverySuccesses->setToolTip(tr("Consecutive successful status checks required after cooldown before automatic routing uses the client again."));
  m_schedule->setToolTip(tr("Outside this local-time window, unattended jobs wait in the persistent queue. Direct named-client sends still work."));
  scheduleRow->setToolTip(tr("Local start and end hour. A start later than the end creates an overnight window."));
  maintenanceForm->addRow(m_reconciliation);
  maintenanceForm->addRow(m_reserveRemaining);
  maintenanceForm->addRow(m_preventDuplicates);
  maintenanceForm->addRow(tr("Reconciliation interval:"), m_reconciliationMinutes);
  maintenanceForm->addRow(m_circuitBreaker);
  maintenanceForm->addRow(tr("Failures before cooldown:"), m_breakerFailures);
  maintenanceForm->addRow(tr("Client cooldown:"), m_breakerCooldown);
  maintenanceForm->addRow(tr("Successful recovery checks:"), m_breakerRecoverySuccesses);
  maintenanceForm->addRow(m_schedule);
  maintenanceForm->addRow(tr("Routing window:"), scheduleRow);
  maintenanceLayout->addLayout(maintenanceForm); maintenanceLayout->addStretch();
  const int maintenanceTab = tabs->addTab(maintenancePage, tr("Maintenance"));
  tabs->setTabToolTip(maintenanceTab, tr("Keep storage estimates aligned with live clients, reserve unfinished downloads, schedule routing and isolate failing clients."));

  auto* clientsPage = new QWidget(tabs);
  auto* clientsLayout = new QVBoxLayout(clientsPage);
  auto* clientsHelp = new QLabel(tr("Limits block unattended routing. Approval-based processing can explicitly override amber load/target warnings, but never red unavailable or insufficient-space states. Zero means no limit."), clientsPage);
  clientsHelp->setWordWrap(true);
  clientsLayout->addWidget(clientsHelp);
  m_clients = new QTableWidget(clientsPage);
  m_clients->setColumnCount(13);
  m_clients->setHorizontalHeaderLabels({tr("Use"), tr("Client"), tr("Max active"), tr("Max managed"), tr("Priority"), tr("Min free GB"), tr("Target free %"), tr("Capacity GB"), tr("Max down MiB/s"), tr("Timeout s"), tr("Retries"), tr("Cleanup"), tr("Storage source")});
  const QStringList clientTips{
    tr("Include this client in automatic routing."),
    tr("Configured torrent client. Its colour comes from Torrent clients settings."),
    tr("Do not send another torrent when this many downloads are active. Zero disables this limit."),
    tr("Maximum RSS Guard-managed torrents retained on this client. Zero disables this limit."),
    tr("Automation preference: 1 is highest priority. Used by Priority, Priority-biased and Balanced routing."),
    tr("Keep at least this much free space after routing a torrent. Zero disables the reserve."),
    tr("Keep at least this percentage of total capacity free after routing. Zero disables the percentage target."),
    tr("Fallback total capacity when the client API cannot report live disk space. Zero means unknown."),
    tr("Treat the client as overloaded above this total download speed. Zero disables this limit."),
    tr("Per-client request timeout. Zero uses the default from Retries and health."),
    tr("Per-client retry count. Minus one uses the default from Retries and health."),
    tr("Permit Safe cleanup on this client. Available only when tested APIs can list and safely remove managed torrents."),
    tr("Shows whether storage decisions use live disk space, a reconciled estimate, a managed-ledger estimate, or no usable figure.")};
  for (int column = 0; column < clientTips.size(); ++column)
    m_clients->horizontalHeaderItem(column)->setToolTip(clientTips.at(column));
  for (int column = 0; column < m_clients->columnCount(); ++column)
    m_clients->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
  m_clients->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  m_clients->horizontalHeader()->setMinimumSectionSize(54);
  m_clients->horizontalHeader()->setStretchLastSection(false);
  m_clients->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_clients->verticalHeader()->setVisible(false);
  clientsLayout->addWidget(m_clients);
  auto* capabilityBox = new QGroupBox(tr("Detected capabilities for selected client"), clientsPage);
  auto* capabilityLayout = new QHBoxLayout(capabilityBox);
  m_capConnected = new QCheckBox(tr("Connected"), capabilityBox);
  m_capStatus = new QCheckBox(tr("Workload"), capabilityBox);
  m_capSpace = new QCheckBox(tr("Disk space"), capabilityBox);
  m_capList = new QCheckBox(tr("Torrent list"), capabilityBox);
  m_capRates = new QCheckBox(tr("Transfer speeds"), capabilityBox);
  m_capRemoval = new QCheckBox(tr("Removal API"), capabilityBox);
  m_capConnected->setToolTip(tr("The latest test successfully connected and authenticated with the client."));
  m_capStatus->setToolTip(tr("The latest test returned live counts for active, queued and seeding torrents."));
  m_capSpace->setToolTip(tr("The latest test returned live free disk space from the client API."));
  m_capList->setToolTip(tr("The latest test returned the torrent list, including a valid empty list."));
  m_capRates->setToolTip(tr("The latest status response contained transfer-rate values used for busy-client routing and active-upload cleanup protection."));
  m_capRemoval->setToolTip(tr("The adapter has a removal operation and listing succeeded. This non-destructive test does not prove that the server account has removal permission."));
  for (QCheckBox* box : {m_capConnected, m_capStatus, m_capSpace, m_capList, m_capRates, m_capRemoval}) {
    box->setEnabled(false); capabilityLayout->addWidget(box);
  }
  capabilityLayout->addStretch();
  clientsLayout->addWidget(capabilityBox);
  m_capabilityTested = new QLabel(clientsPage); m_capabilityTested->setWordWrap(true);
  clientsLayout->addWidget(m_capabilityTested);
  auto* testButtons = new QHBoxLayout();
  m_testSelected = new QPushButton(tr("Test selected client"), clientsPage);
  m_testAll = new QPushButton(tr("Test all clients"), clientsPage);
  m_testSelected->setToolTip(tr("Run a non-destructive connection and capability test for the selected client. No torrent is added or removed."));
  m_testAll->setToolTip(tr("Run the same non-destructive test for every client participating in automation."));
  testButtons->addStretch(); testButtons->addWidget(m_testSelected); testButtons->addWidget(m_testAll);
  clientsLayout->addLayout(testButtons);
  const int clientsTab = tabs->addTab(clientsPage, tr("Clients and limits"));
  tabs->setTabToolTip(clientsTab, tr("Choose participating clients, set safety limits and priorities, and detect supported monitoring features."));

  auto* rulesPage = new QWidget(tabs);
  auto* rulesLayout = new QVBoxLayout(rulesPage);
  auto* rulesHelp = new QLabel(tr("Rules are checked from top to bottom. With no rules, every new RSS item containing a torrent link is eligible."), rulesPage);
  rulesHelp->setWordWrap(true);
  rulesLayout->addWidget(rulesHelp);
  m_rules = new QListWidget(rulesPage);
  m_rules->setToolTip(tr("Checked rules are evaluated from top to bottom. Double-click a rule to edit it."));
  rulesLayout->addWidget(m_rules, 1);
  auto* ruleButtons = new QHBoxLayout();
  auto* add = new QPushButton(tr("Add rule"), rulesPage);
  m_editRule = new QPushButton(tr("Edit"), rulesPage);
  auto* duplicate = new QPushButton(tr("Duplicate"), rulesPage);
  auto* moveUp = new QPushButton(tr("Move up"), rulesPage);
  auto* moveDown = new QPushButton(tr("Move down"), rulesPage);
  m_removeRule = new QPushButton(tr("Remove"), rulesPage);
  auto* testRules = new QPushButton(tr("Test rules against latest items"), rulesPage);
  for (QPushButton* button : {m_editRule, duplicate, moveUp, moveDown, m_removeRule}) button->setEnabled(false);
  add->setToolTip(tr("Create a rule using feeds already configured in RSS Guard, text filters, and allowed clients."));
  m_editRule->setToolTip(tr("Edit the selected automation rule."));
  duplicate->setToolTip(tr("Copy the selected rule so a similar rule can be configured quickly."));
  moveUp->setToolTip(tr("Move the selected rule earlier. The first matching enabled rule wins."));
  moveDown->setToolTip(tr("Move the selected rule later. The first matching enabled rule wins."));
  m_removeRule->setToolTip(tr("Remove the selected automation rule."));
  testRules->setToolTip(tr("Run a dry explanation against the most recently fetched items. Matching, routing and cleanup decisions appear in Activity without changing a client."));
  ruleButtons->addWidget(add); ruleButtons->addWidget(m_editRule); ruleButtons->addWidget(duplicate);
  ruleButtons->addWidget(moveUp); ruleButtons->addWidget(moveDown); ruleButtons->addWidget(m_removeRule);
  ruleButtons->addStretch(); ruleButtons->addWidget(testRules);
  rulesLayout->addLayout(ruleButtons);
  const int rulesTab = tabs->addTab(rulesPage, tr("RSS rules"));
  tabs->setTabToolTip(rulesTab, tr("Control which incoming RSS items qualify and which clients they may use."));

  auto* cleanupPage = new QWidget(tabs);
  auto* cleanupLayout = new QVBoxLayout(cleanupPage);
  auto* warning = new QLabel(tr("Cleanup is destructive. It only considers completed torrents carrying RSS Guard's automation marker; manually added torrents are never eligible."), cleanupPage);
  warning->setWordWrap(true);
  cleanupLayout->addWidget(warning);
  auto* cleanupForm = new QFormLayout();
  m_cleanup = new QCheckBox(tr("Allow automatic cleanup when a client is below its free-space limit"), cleanupPage);
  m_deleteData = new QCheckBox(tr("Delete downloaded data as well as the torrent"), cleanupPage);
  m_confirmCleanup = new QCheckBox(tr("Ask before every removal"), cleanupPage);
  m_seedHoursEnabled = new QCheckBox(tr("Use"), cleanupPage);
  m_seedHours = new QSpinBox(cleanupPage); m_seedHours->setRange(0, 100000); m_seedHours->setSuffix(tr(" hours"));
  m_ratioEnabled = new QCheckBox(tr("Use"), cleanupPage);
  m_ratio = new QDoubleSpinBox(cleanupPage); m_ratio->setRange(0, 10000); m_ratio->setDecimals(2);
  m_inactiveHoursEnabled = new QCheckBox(tr("Use"), cleanupPage);
  m_inactiveHours = new QSpinBox(cleanupPage); m_inactiveHours->setRange(0, 100000); m_inactiveHours->setSuffix(tr(" hours"));
  m_maxRemovalsEnabled = new QCheckBox(tr("Use"), cleanupPage);
  m_maxRemovals = new QSpinBox(cleanupPage); m_maxRemovals->setRange(1, 100);
  m_cleanupStopGbEnabled = new QCheckBox(tr("Use"), cleanupPage);
  m_cleanupStopGb = new QDoubleSpinBox(cleanupPage); m_cleanupStopGb->setRange(0, 1000000); m_cleanupStopGb->setSuffix(tr(" GB"));
  m_protectUploading = new QCheckBox(tr("Protect torrents uploading above"), cleanupPage);
  m_protectUploadKib = new QSpinBox(cleanupPage); m_protectUploadKib->setRange(1, 100000000); m_protectUploadKib->setSuffix(tr(" KiB/s"));
  m_protectUnknownSpeed = new QCheckBox(tr("Protect a torrent when its upload speed is unavailable"), cleanupPage);
  m_protectRecentHours = new QSpinBox(cleanupPage); m_protectRecentHours->setRange(0, 8760); m_protectRecentHours->setSuffix(tr(" hours"));
  m_cleanupGrace = new QCheckBox(tr("Use a grace period before permanent removal"), cleanupPage);
  m_cleanupGraceHours = new QSpinBox(cleanupPage); m_cleanupGraceHours->setRange(1, 8760); m_cleanupGraceHours->setSuffix(tr(" hours"));
  m_smartCleanup = new QCheckBox(tr("Use smart cleanup scoring"), cleanupPage);
  m_minimumCopiesEnabled = new QCheckBox(tr("Keep completed copies across all clients"), cleanupPage);
  m_minimumCopies = new QSpinBox(cleanupPage); m_minimumCopies->setRange(1, 20);
  m_protectedTags = new QLineEdit(cleanupPage);
  m_protectedTrackers = new QLineEdit(cleanupPage);
  m_cleanupSchedule = new QCheckBox(tr("Only perform cleanup during these hours"), cleanupPage);
  m_cleanupScheduleStart = new QComboBox(cleanupPage); m_cleanupScheduleEnd = new QComboBox(cleanupPage);
  for (int hour = 0; hour <= 24; ++hour) {
    const QString label = QStringLiteral("%1:00").arg(hour, 2, 10, QLatin1Char('0'));
    if (hour < 24) m_cleanupScheduleStart->addItem(label, hour);
    m_cleanupScheduleEnd->addItem(label, hour);
  }
  auto* cleanupWindow = new QWidget(cleanupPage); auto* cleanupWindowLayout = new QHBoxLayout(cleanupWindow);
  cleanupWindowLayout->setContentsMargins(0, 0, 0, 0); cleanupWindowLayout->addWidget(m_cleanupScheduleStart); cleanupWindowLayout->addWidget(new QLabel(tr("to"), cleanupWindow)); cleanupWindowLayout->addWidget(m_cleanupScheduleEnd);
  m_cleanupBatchPercent = new QDoubleSpinBox(cleanupPage); m_cleanupBatchPercent->setRange(0, 100); m_cleanupBatchPercent->setDecimals(1); m_cleanupBatchPercent->setSuffix(tr(" %"));
  m_cleanup->setToolTip(tr("Allow cleanup only when routing is blocked because an opted-in client is below its minimum-free-space limit."));
  m_deleteData->setToolTip(tr("Also erase downloaded files. Leave off to remove only the torrent job. This action cannot be undone."));
  m_confirmCleanup->setToolTip(tr("Ask for approval before every removal. Recommended while validating your rules and limits."));
  m_seedHours->setToolTip(tr("A completed managed torrent must have seeded for at least this many hours before it can be considered."));
  m_ratio->setToolTip(tr("A managed torrent must reach at least this share ratio before it can be considered for cleanup."));
  m_inactiveHours->setToolTip(tr("A managed torrent must have no recent transfer activity for at least this many hours."));
  m_maxRemovals->setToolTip(tr("Hard limit on the number of torrents automation may remove during one processing run."));
  m_cleanupStopGb->setToolTip(tr("Stop removing torrents once the client reaches this amount of free space."));
  m_seedHoursEnabled->setToolTip(tr("When enabled, only torrents old enough to meet the completed/seeding age can be removed."));
  m_ratioEnabled->setToolTip(tr("When enabled, only torrents meeting the minimum share ratio can be removed."));
  m_inactiveHoursEnabled->setToolTip(tr("When enabled, only torrents inactive for this long can be removed."));
  m_maxRemovalsEnabled->setToolTip(tr("When disabled, RSS Guard still applies an internal emergency maximum of 25 removals per run."));
  m_cleanupStopGbEnabled->setToolTip(tr("When enabled, cleanup continues until this target free-space level is reached."));
  m_protectUploading->setToolTip(tr("Skip an otherwise eligible old torrent for this cleanup session while its current upload speed is at or above the chosen threshold."));
  m_protectUploadKib->setToolTip(tr("Per-torrent upload-speed threshold. Skipped torrents are reconsidered during the next cleanup session."));
  m_protectUnknownSpeed->setToolTip(tr("Safest behaviour for adapters that cannot report a per-torrent upload speed."));
  m_protectRecentHours->setToolTip(tr("Protect a torrent for this long after RSS Guard last observed it uploading. Zero disables historical upload protection."));
  m_cleanupGrace->setToolTip(tr("First mark a torrent as a cleanup candidate, then check it again after the grace period before removal."));
  m_cleanupGraceHours->setToolTip(tr("Minimum wait between selecting a cleanup candidate and allowing its permanent removal."));
  m_smartCleanup->setToolTip(tr("Rank eligible torrents using age, recoverable size and ratio. Disable to use oldest-completed-first ordering."));
  m_minimumCopiesEnabled->setToolTip(tr("Do not remove a completed torrent if that would leave fewer than the selected number of completed copies across reachable clients."));
  m_minimumCopies->setToolTip(tr("Minimum completed copies retained across the configured client pool."));
  m_protectedTags->setToolTip(tr("Comma-separated exact tags or labels that must never be removed automatically, for example: keep, archive."));
  m_protectedTrackers->setToolTip(tr("Comma-separated tracker host text that must never be removed automatically, for example: tracker.example."));
  m_cleanupSchedule->setToolTip(tr("Queue cleanup until this local-time maintenance window. Routing may still use another client outside the cleanup window."));
  m_cleanupBatchPercent->setToolTip(tr("Round the required free-space target upward by this percentage of total capacity. Zero disables batch rounding."));
  const auto optionalControl = [cleanupPage](QCheckBox* enabled, QWidget* editor) {
    auto* container = new QWidget(cleanupPage);
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(enabled);
    layout->addWidget(editor, 1);
    return container;
  };
  cleanupForm->addRow(m_cleanup);
  cleanupForm->addRow(m_deleteData);
  cleanupForm->addRow(m_confirmCleanup);
  cleanupForm->addRow(tr("Minimum completed/seeding age:"), optionalControl(m_seedHoursEnabled, m_seedHours));
  cleanupForm->addRow(tr("Minimum ratio:"), optionalControl(m_ratioEnabled, m_ratio));
  cleanupForm->addRow(tr("Minimum inactivity:"), optionalControl(m_inactiveHoursEnabled, m_inactiveHours));
  cleanupForm->addRow(tr("Maximum removals per run:"), optionalControl(m_maxRemovalsEnabled, m_maxRemovals));
  cleanupForm->addRow(tr("Target free space after cleanup:"), optionalControl(m_cleanupStopGbEnabled, m_cleanupStopGb));
  cleanupForm->addRow(m_protectUploading, m_protectUploadKib);
  cleanupForm->addRow(m_protectUnknownSpeed);
  cleanupForm->addRow(tr("Protect after recent upload:"), m_protectRecentHours);
  cleanupForm->addRow(m_cleanupGrace, m_cleanupGraceHours);
  cleanupForm->addRow(m_smartCleanup);
  cleanupForm->addRow(m_minimumCopiesEnabled, m_minimumCopies);
  cleanupForm->addRow(tr("Never remove tags:"), m_protectedTags);
  cleanupForm->addRow(tr("Never remove trackers containing:"), m_protectedTrackers);
  cleanupForm->addRow(m_cleanupSchedule, cleanupWindow);
  cleanupForm->addRow(tr("Cleanup space batch:"), m_cleanupBatchPercent);
  cleanupLayout->addLayout(cleanupForm);
  cleanupLayout->addStretch();
  const int cleanupTab = tabs->addTab(cleanupPage, tr("Safe cleanup"));
  tabs->setTabToolTip(cleanupTab, tr("Optionally remove only completed torrents marked as managed by RSS Guard, subject to every safety threshold."));

  auto* activityPage = new QWidget(tabs);
  auto* activityLayout = new QVBoxLayout(activityPage);
  m_activity = new QListWidget(activityPage);
  m_activity->setWordWrap(true);
  m_queue = new QListWidget(activityPage);
  m_queue->setToolTip(tr("Persistent items waiting for a retry, maintenance window or healthy destination."));
  auto* refresh = new QPushButton(tr("Refresh activity"), activityPage);
  auto* retryQueued = new QPushButton(tr("Retry selected now"), activityPage);
  auto* retryAllQueued = new QPushButton(tr("Retry all now"), activityPage);
  auto* chooseQueued = new QPushButton(tr("Choose client and send"), activityPage);
  auto* cancelQueued = new QPushButton(tr("Cancel selected"), activityPage);
  auto* cancelAllQueued = new QPushButton(tr("Cancel all"), activityPage);
  auto* exportActivityButton = new QPushButton(tr("Export activity"), activityPage);
  auto* clearActivityButton = new QPushButton(tr("Clear activity"), activityPage);
  m_activity->setToolTip(tr("Newest recorded routing, retry, failure, dry-run and cleanup decisions appear at the top."));
  refresh->setToolTip(tr("Reload the latest automation events from the in-memory activity history."));
  activityLayout->addWidget(new QLabel(tr("Pending automation queue:"), activityPage));
  activityLayout->addWidget(m_queue, 1);
  auto* queueButtons = new QHBoxLayout(); queueButtons->addWidget(retryQueued); queueButtons->addWidget(retryAllQueued);
  queueButtons->addWidget(chooseQueued); queueButtons->addWidget(cancelQueued); queueButtons->addWidget(cancelAllQueued); queueButtons->addStretch();
  activityLayout->addLayout(queueButtons);
  activityLayout->addWidget(new QLabel(tr("Decision history:"), activityPage));
  activityLayout->addWidget(m_activity, 2);
  auto* activityButtons = new QHBoxLayout(); activityButtons->addStretch(); activityButtons->addWidget(exportActivityButton);
  activityButtons->addWidget(clearActivityButton); activityButtons->addWidget(refresh); activityLayout->addLayout(activityButtons);
  const int activityTab = tabs->addTab(activityPage, tr("Activity"));
  tabs->setTabToolTip(activityTab, tr("Review what automation decided and why. Dry-run decisions are recorded here too."));

  const QList<QObject*> dirtyObjects{m_enabled, m_dryRun, m_notifications, m_strategy, m_historyLimit, m_unknownSizeGb,
                                     m_retryEnabled, m_retryAttempts, m_retryInitialSeconds, m_retryMaximumSeconds,
                                     m_retryBackoff, m_requestTimeoutSeconds,
                                     m_reconciliation, m_reserveRemaining, m_preventDuplicates, m_reconciliationMinutes,
                                     m_circuitBreaker, m_breakerFailures, m_breakerCooldown,
                                     m_breakerRecoverySuccesses,
                                     m_schedule, m_scheduleStart, m_scheduleEnd,
                                     m_cleanup, m_deleteData, m_confirmCleanup, m_seedHours, m_ratio,
                                     m_inactiveHours, m_maxRemovals, m_cleanupStopGb, m_seedHoursEnabled,
                                     m_ratioEnabled, m_inactiveHoursEnabled, m_maxRemovalsEnabled,
                                     m_cleanupStopGbEnabled, m_protectUploading, m_protectUploadKib,
                                     m_protectUnknownSpeed, m_protectRecentHours, m_cleanupGrace,
                                     m_cleanupGraceHours, m_smartCleanup, m_minimumCopiesEnabled,
                                     m_minimumCopies, m_protectedTags, m_protectedTrackers,
                                     m_cleanupSchedule, m_cleanupScheduleStart, m_cleanupScheduleEnd,
                                     m_cleanupBatchPercent, m_clients};
  for (QObject* object : dirtyObjects) {
    if (auto* box = qobject_cast<QCheckBox*>(object)) connect(box, &QCheckBox::toggled, this, &SettingsTorrentAutomation::dirtifySettings);
    else if (auto* combo = qobject_cast<QComboBox*>(object)) connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsTorrentAutomation::dirtifySettings);
    else if (auto* spin = qobject_cast<QSpinBox*>(object)) connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsTorrentAutomation::dirtifySettings);
    else if (auto* dspin = qobject_cast<QDoubleSpinBox*>(object)) connect(dspin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SettingsTorrentAutomation::dirtifySettings);
    else if (auto* line = qobject_cast<QLineEdit*>(object)) connect(line, &QLineEdit::textChanged, this, &SettingsTorrentAutomation::dirtifySettings);
  }
  connect(m_clients, &QTableWidget::cellChanged, this, &SettingsTorrentAutomation::dirtifySettings);
  connect(m_clients, &QTableWidget::currentCellChanged, this, [this]() { updateCapabilityDisplay(); });
  connect(m_testSelected, &QPushButton::clicked, this, &SettingsTorrentAutomation::testSelectedClient);
  connect(m_testAll, &QPushButton::clicked, this, &SettingsTorrentAutomation::testAllClients);
  connect(add, &QPushButton::clicked, this, &SettingsTorrentAutomation::addRule);
  connect(m_editRule, &QPushButton::clicked, this, &SettingsTorrentAutomation::editRule);
  connect(duplicate, &QPushButton::clicked, this, &SettingsTorrentAutomation::duplicateRule);
  connect(moveUp, &QPushButton::clicked, this, &SettingsTorrentAutomation::moveRuleUp);
  connect(moveDown, &QPushButton::clicked, this, &SettingsTorrentAutomation::moveRuleDown);
  connect(m_removeRule, &QPushButton::clicked, this, &SettingsTorrentAutomation::removeRule);
  connect(m_rules, &QListWidget::itemDoubleClicked, this, [this]() { editRule(); });
  connect(m_rules, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
    const int row = m_rules->row(item);
    if (row >= 0 && row < m_config.rules.size()) {
      m_config.rules[row].enabled = item->checkState() == Qt::Checked;
      dirtifySettings();
    }
  });
  connect(m_rules, &QListWidget::currentRowChanged, this,
          [this, duplicate, moveUp, moveDown](int row) {
    m_editRule->setEnabled(row >= 0); m_removeRule->setEnabled(row >= 0); duplicate->setEnabled(row >= 0);
    moveUp->setEnabled(row > 0); moveDown->setEnabled(row >= 0 && row + 1 < m_config.rules.size());
  });
  connect(refresh, &QPushButton::clicked, this, &SettingsTorrentAutomation::refreshActivity);
  connect(m_cleanup, &QCheckBox::toggled, this, &SettingsTorrentAutomation::updateCleanupControls);
  for (QCheckBox* option : {m_seedHoursEnabled, m_ratioEnabled, m_inactiveHoursEnabled,
                             m_maxRemovalsEnabled, m_cleanupStopGbEnabled})
    connect(option, &QCheckBox::toggled, this, &SettingsTorrentAutomation::updateCleanupControls);
  connect(m_protectUploading, &QCheckBox::toggled, this, &SettingsTorrentAutomation::updateCleanupControls);
  connect(m_cleanupGrace, &QCheckBox::toggled, this, &SettingsTorrentAutomation::updateCleanupControls);
  connect(m_minimumCopiesEnabled, &QCheckBox::toggled, this, &SettingsTorrentAutomation::updateCleanupControls);
  connect(m_cleanupSchedule, &QCheckBox::toggled, this, &SettingsTorrentAutomation::updateCleanupControls);
  const auto updateMaintenanceControls = [this]() {
    m_reconciliationMinutes->setEnabled(m_reconciliation->isChecked());
    m_reserveRemaining->setEnabled(m_reconciliation->isChecked());
    m_breakerFailures->setEnabled(m_circuitBreaker->isChecked());
    m_breakerCooldown->setEnabled(m_circuitBreaker->isChecked());
    m_breakerRecoverySuccesses->setEnabled(m_circuitBreaker->isChecked());
    m_scheduleStart->setEnabled(m_schedule->isChecked());
    m_scheduleEnd->setEnabled(m_schedule->isChecked());
  };
  connect(m_reconciliation, &QCheckBox::toggled, this, updateMaintenanceControls);
  connect(m_circuitBreaker, &QCheckBox::toggled, this, updateMaintenanceControls);
  connect(m_schedule, &QCheckBox::toggled, this, updateMaintenanceControls);
  updateMaintenanceControls();
  connect(m_runDryTest, &QPushButton::clicked, this, &SettingsTorrentAutomation::runDryTest);
  connect(readinessAudit, &QPushButton::clicked, this, &SettingsTorrentAutomation::runReadinessAudit);
  connect(testRules, &QPushButton::clicked, this, &SettingsTorrentAutomation::runDryTest);
  connect(exportConfiguration, &QPushButton::clicked, this, &SettingsTorrentAutomation::exportConfiguration);
  connect(importConfiguration, &QPushButton::clicked, this, &SettingsTorrentAutomation::importConfiguration);
  connect(retryQueued, &QPushButton::clicked, this, &SettingsTorrentAutomation::retryQueuedItem);
  connect(retryAllQueued, &QPushButton::clicked, this, &SettingsTorrentAutomation::retryAllQueuedItems);
  connect(chooseQueued, &QPushButton::clicked, this, &SettingsTorrentAutomation::chooseQueuedClient);
  connect(cancelQueued, &QPushButton::clicked, this, &SettingsTorrentAutomation::cancelQueuedItem);
  connect(cancelAllQueued, &QPushButton::clicked, this, &SettingsTorrentAutomation::cancelAllQueuedItems);
  connect(exportActivityButton, &QPushButton::clicked, this, &SettingsTorrentAutomation::exportActivity);
  connect(clearActivityButton, &QPushButton::clicked, this, &SettingsTorrentAutomation::clearActivity);
  connect(TorrentAutomationEngine::instance(qApp), &TorrentAutomationEngine::activityAdded,
          this, [this]() { refreshActivity(); });
  connect(m_cleanup, &QCheckBox::clicked, this, [this](bool checked) {
    if (checked) QMessageBox::warning(this, tr("Automatic cleanup warning"),
      tr("Automatic cleanup can remove torrents. Keep Dry run and Ask before every removal enabled until you have reviewed the Activity results."));
  });
  connect(m_deleteData, &QCheckBox::clicked, this, [this](bool checked) {
    if (checked) QMessageBox::warning(this, tr("Downloaded data deletion warning"),
      tr("This option permanently deletes downloaded files as well as removing the torrent. This cannot be undone."));
  });
  connect(m_confirmCleanup, &QCheckBox::clicked, this, [this](bool checked) {
    if (!checked && m_cleanup->isChecked()) QMessageBox::warning(this, tr("Cleanup confirmation disabled"),
      tr("Live cleanup will be able to remove eligible torrents without asking you first."));
  });
  for (QCheckBox* option : {m_seedHoursEnabled, m_ratioEnabled, m_inactiveHoursEnabled}) {
    connect(option, &QCheckBox::clicked, this, [this](bool checked) {
      if (!checked && m_cleanup->isChecked()) QMessageBox::warning(this, tr("Cleanup safeguard disabled"),
        tr("A cleanup eligibility safeguard has been disabled. Review a manual dry test before enabling live automation."));
    });
  }
  connect(m_maxRemovalsEnabled, &QCheckBox::clicked, this, [this](bool checked) {
    if (!checked && m_cleanup->isChecked()) QMessageBox::warning(this, tr("Cleanup limit disabled"),
      tr("The chosen per-run limit is disabled. RSS Guard will still enforce an internal maximum of 25 removals per run."));
  });
  connect(m_cleanupStopGbEnabled, &QCheckBox::clicked, this, [this](bool checked) {
    if (!checked && m_cleanup->isChecked()) QMessageBox::warning(this, tr("Cleanup target disabled"),
      tr("Cleanup will stop as soon as the client meets its own minimum-free-space requirement."));
  });
  connect(m_cleanupGrace, &QCheckBox::clicked, this, [this](bool checked) {
    if (!checked && m_cleanup->isChecked()) QMessageBox::warning(this, tr("Cleanup grace period disabled"),
      tr("Eligible torrents may be removed during their first cleanup assessment. Keep per-removal confirmation enabled while testing."));
  });
  connect(m_minimumCopiesEnabled, &QCheckBox::clicked, this, [this](bool checked) {
    if (!checked && m_cleanup->isChecked()) QMessageBox::warning(this, tr("Copy protection disabled"),
      tr("Cleanup will not check whether another completed copy exists on a different configured client."));
  });
  connect(m_reconciliation, &QCheckBox::clicked, this, [this](bool checked) {
    if (!checked) QMessageBox::warning(this, tr("Storage reconciliation disabled"),
      tr("Manual capacity estimates can drift when torrents are changed outside RSS Guard. Live free-space readings remain preferred where available."));
  });
  connect(m_maxRemovals, &QAbstractSpinBox::editingFinished, this, [this]() {
    if (m_cleanup->isChecked() && m_maxRemovalsEnabled->isChecked() && m_maxRemovals->value() > 10)
      QMessageBox::warning(this, tr("High cleanup removal limit"),
        tr("Allowing more than 10 removals in one run is potentially destructive. Run a dry test and keep per-removal confirmation enabled."));
  });
  SettingsPanel::loadUi();
}

void SettingsTorrentAutomation::loadSettings() {
  onBeginLoadSettings();
  m_config = TorrentAutomationConfig::load(settings());
  m_enabled->setChecked(m_config.enabled);
  m_dryRun->setChecked(m_config.dryRun);
  m_notifications->setChecked(m_config.showNotifications);
  m_strategy->setCurrentIndex(m_strategy->findData(static_cast<int>(m_config.strategy)));
  m_retryEnabled->setChecked(m_config.retryEnabled);
  m_retryAttempts->setValue(m_config.retryAttempts);
  m_retryInitialSeconds->setValue(m_config.retryInitialSeconds);
  m_retryMaximumSeconds->setValue(m_config.retryMaximumSeconds);
  m_retryBackoff->setChecked(m_config.retryExponentialBackoff);
  m_requestTimeoutSeconds->setValue(m_config.requestTimeoutSeconds);
  m_historyLimit->setValue(m_config.historyLimit);
  m_unknownSizeGb->setValue(m_config.unknownTorrentSizeBytes / GiB);
  m_reconciliation->setChecked(m_config.reconciliationEnabled);
  m_reserveRemaining->setChecked(m_config.reserveRemainingBytes);
  m_preventDuplicates->setChecked(m_config.preventDuplicateAcrossClients);
  m_reconciliationMinutes->setValue(m_config.reconciliationMinutes);
  m_circuitBreaker->setChecked(m_config.circuitBreakerEnabled);
  m_breakerFailures->setValue(m_config.circuitBreakerFailures);
  m_breakerCooldown->setValue(m_config.circuitBreakerCooldownMinutes);
  m_breakerRecoverySuccesses->setValue(m_config.circuitBreakerRecoverySuccesses);
  m_schedule->setChecked(m_config.scheduleEnabled);
  m_scheduleStart->setCurrentIndex(m_scheduleStart->findData(m_config.scheduleStartHour));
  m_scheduleEnd->setCurrentIndex(m_scheduleEnd->findData(m_config.scheduleEndHour));
  m_cleanup->setChecked(m_config.cleanupEnabled);
  m_deleteData->setChecked(m_config.deleteData);
  m_confirmCleanup->setChecked(m_config.cleanupRequireConfirmation);
  m_seedHoursEnabled->setChecked(m_config.minimumSeedHoursEnabled);
  m_seedHours->setValue(m_config.minimumSeedHours);
  m_ratioEnabled->setChecked(m_config.minimumRatioEnabled);
  m_ratio->setValue(m_config.minimumRatio);
  m_inactiveHoursEnabled->setChecked(m_config.minimumInactiveHoursEnabled);
  m_inactiveHours->setValue(m_config.minimumInactiveHours);
  m_maxRemovalsEnabled->setChecked(m_config.maximumRemovalsEnabled);
  m_maxRemovals->setValue(m_config.maximumRemovalsPerRun);
  m_cleanupStopGbEnabled->setChecked(m_config.cleanupStopFreeEnabled);
  m_cleanupStopGb->setValue(m_config.cleanupStopFreeBytes / GiB);
  m_protectUploading->setChecked(m_config.protectUploadingEnabled);
  m_protectUploadKib->setValue(int(m_config.protectUploadBytesPerSecond / 1024));
  m_protectUnknownSpeed->setChecked(m_config.protectWhenSpeedUnknown);
  m_protectRecentHours->setValue(m_config.protectRecentUploadHours);
  m_cleanupGrace->setChecked(m_config.cleanupGraceEnabled);
  m_cleanupGraceHours->setValue(m_config.cleanupGraceHours);
  m_smartCleanup->setChecked(m_config.smartCleanupOrder);
  m_minimumCopiesEnabled->setChecked(m_config.minimumCopiesEnabled);
  m_minimumCopies->setValue(m_config.minimumCopiesAcrossClients);
  m_protectedTags->setText(m_config.protectedTags.join(QStringLiteral(", ")));
  m_protectedTrackers->setText(m_config.protectedTrackerTerms.join(QStringLiteral(", ")));
  m_cleanupSchedule->setChecked(m_config.cleanupScheduleEnabled);
  m_cleanupScheduleStart->setCurrentIndex(m_cleanupScheduleStart->findData(m_config.cleanupScheduleStartHour));
  m_cleanupScheduleEnd->setCurrentIndex(m_cleanupScheduleEnd->findData(m_config.cleanupScheduleEndHour));
  m_cleanupBatchPercent->setValue(m_config.cleanupBatchPercent);
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
  m_config.retryMinutes = qMax(1, m_retryInitialSeconds->value() / 60);
  m_config.retryEnabled = m_retryEnabled->isChecked();
  m_config.retryAttempts = m_retryAttempts->value();
  m_config.retryInitialSeconds = m_retryInitialSeconds->value();
  m_config.retryMaximumSeconds = m_retryMaximumSeconds->value();
  m_config.retryExponentialBackoff = m_retryBackoff->isChecked();
  m_config.requestTimeoutSeconds = m_requestTimeoutSeconds->value();
  m_config.historyLimit = m_historyLimit->value();
  m_config.unknownTorrentSizeBytes = qint64(m_unknownSizeGb->value() * GiB);
  m_config.reconciliationEnabled = m_reconciliation->isChecked();
  m_config.reserveRemainingBytes = m_reserveRemaining->isChecked();
  m_config.preventDuplicateAcrossClients = m_preventDuplicates->isChecked();
  m_config.reconciliationMinutes = m_reconciliationMinutes->value();
  m_config.circuitBreakerEnabled = m_circuitBreaker->isChecked();
  m_config.circuitBreakerFailures = m_breakerFailures->value();
  m_config.circuitBreakerCooldownMinutes = m_breakerCooldown->value();
  m_config.circuitBreakerRecoverySuccesses = m_breakerRecoverySuccesses->value();
  m_config.scheduleEnabled = m_schedule->isChecked();
  m_config.scheduleStartHour = m_scheduleStart->currentData().toInt();
  m_config.scheduleEndHour = m_scheduleEnd->currentData().toInt();
  m_config.cleanupEnabled = m_cleanup->isChecked();
  m_config.deleteData = m_deleteData->isChecked();
  m_config.cleanupRequireConfirmation = m_confirmCleanup->isChecked();
  m_config.minimumSeedHoursEnabled = m_seedHoursEnabled->isChecked();
  m_config.minimumSeedHours = m_seedHours->value();
  m_config.minimumRatioEnabled = m_ratioEnabled->isChecked();
  m_config.minimumRatio = m_ratio->value();
  m_config.minimumInactiveHoursEnabled = m_inactiveHoursEnabled->isChecked();
  m_config.minimumInactiveHours = m_inactiveHours->value();
  m_config.maximumRemovalsEnabled = m_maxRemovalsEnabled->isChecked();
  m_config.maximumRemovalsPerRun = m_maxRemovals->value();
  m_config.cleanupStopFreeEnabled = m_cleanupStopGbEnabled->isChecked();
  m_config.cleanupStopFreeBytes = qint64(m_cleanupStopGb->value() * GiB);
  m_config.protectUploadingEnabled = m_protectUploading->isChecked();
  m_config.protectUploadBytesPerSecond = qint64(m_protectUploadKib->value()) * 1024;
  m_config.protectWhenSpeedUnknown = m_protectUnknownSpeed->isChecked();
  m_config.protectRecentUploadHours = m_protectRecentHours->value();
  m_config.cleanupGraceEnabled = m_cleanupGrace->isChecked();
  m_config.cleanupGraceHours = m_cleanupGraceHours->value();
  m_config.smartCleanupOrder = m_smartCleanup->isChecked();
  m_config.minimumCopiesEnabled = m_minimumCopiesEnabled->isChecked();
  m_config.minimumCopiesAcrossClients = m_minimumCopies->value();
  m_config.protectedTags = m_protectedTags->text().split(QLatin1Char(','), Qt::SkipEmptyParts);
  for (QString& value : m_config.protectedTags) value = value.trimmed();
  m_config.protectedTrackerTerms = m_protectedTrackers->text().split(QLatin1Char(','), Qt::SkipEmptyParts);
  for (QString& value : m_config.protectedTrackerTerms) value = value.trimmed();
  m_config.cleanupScheduleEnabled = m_cleanupSchedule->isChecked();
  m_config.cleanupScheduleStartHour = m_cleanupScheduleStart->currentData().toInt();
  m_config.cleanupScheduleEndHour = m_cleanupScheduleEnd->currentData().toInt();
  m_config.cleanupBatchPercent = m_cleanupBatchPercent->value();
  m_config.clients.clear();
  for (int row = 0; row < m_clients->rowCount(); ++row) {
    TorrentAutomationClientPolicy policy;
    policy.clientId = m_clients->item(row, 1)->data(Qt::UserRole).toString();
    policy.enabled = m_clients->item(row, 0)->checkState() == Qt::Checked;
    policy.maxActiveDownloads = m_clients->item(row, 2)->text().toInt();
    policy.maxManagedTorrents = m_clients->item(row, 3)->text().toInt();
    policy.priority = qMax(1, m_clients->item(row, 4)->text().toInt());
    policy.minimumFreeBytes = qint64(m_clients->item(row, 5)->text().toDouble() * GiB);
    policy.targetFreePercent = qBound(0.0, m_clients->item(row, 6)->text().toDouble(), 100.0);
    policy.configuredCapacityBytes = qint64(m_clients->item(row, 7)->text().toDouble() * GiB);
    policy.maximumDownloadBytesPerSecond = qMax<qint64>(0, qint64(m_clients->item(row, 8)->text().toDouble() * 1024.0 * 1024.0));
    policy.requestTimeoutSeconds = qMax(0, m_clients->item(row, 9)->text().toInt());
    policy.retryAttempts = qMax(-1, m_clients->item(row, 10)->text().toInt());
    policy.allowCleanup = m_clients->item(row, 11)->checkState() == Qt::Checked;
    m_config.clients.append(policy);
  }
  m_config.save(settings());
  onEndSaveSettings();
}

void SettingsTorrentAutomation::refreshClientPolicies() {
  m_clientConfigs = TorrentClientConfig::enabledInPriorityOrder(TorrentClientConfig::load(settings()));
  m_clients->blockSignals(true);
  m_clients->setRowCount(m_clientConfigs.size());
  for (int row = 0; row < m_clientConfigs.size(); ++row) {
    const TorrentClientConfig& client = m_clientConfigs.at(row);
    TorrentAutomationClientPolicy policy = m_config.policyFor(client.id);
    const bool hasSavedPolicy = std::any_of(m_config.clients.cbegin(), m_config.clients.cend(),
      [&client](const TorrentAutomationClientPolicy& saved) { return saved.clientId == client.id; });
    if (!hasSavedPolicy) policy.priority = qMax(1, client.priority);
    auto* use = new QTableWidgetItem();
    use->setCheckState(policy.enabled ? Qt::Checked : Qt::Unchecked);
    use->setTextAlignment(Qt::AlignCenter);
    const QString typeName = TorrentClientConfig::typeName(client.type);
    auto* name = new QTableWidgetItem(QStringLiteral("%1\n%2").arg(client.name, typeName));
    name->setData(Qt::UserRole, client.id);
    name->setFlags(name->flags() & ~Qt::ItemIsEditable);
    name->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    if (client.colorSettingsLists && !client.buttonColor.isEmpty()) {
      const int swatchSize = qMax(12, m_clients->fontMetrics().height());
      QPixmap swatch(swatchSize, swatchSize);
      swatch.fill(Qt::transparent);
      QPainter painter(&swatch);
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(client.buttonColor));
      painter.drawRoundedRect(swatch.rect().adjusted(1, 1, -1, -1), 2, 2);
      name->setIcon(QIcon(swatch));
    }
    const int clientRowHeight = m_clients->fontMetrics().lineSpacing() * 2 + 12;
    name->setSizeHint(QSize(0, clientRowHeight));
    name->setToolTip(client.capabilityTested
      ? tr("Last capability test: %1\n%2").arg(QLocale().toString(client.capabilityTestedAt.toLocalTime(), QLocale::ShortFormat),
                                               client.capabilityDetail)
      : tr("%1\nCapabilities not tested yet. Select this client and choose Test selected client.").arg(typeName));
    auto* cleanup = new QTableWidgetItem();
    cleanup->setTextAlignment(Qt::AlignCenter);
    const bool cleanupSupported = client.capabilityTested && client.capabilityTorrentList && client.capabilityRemoval;
    cleanup->setCheckState(cleanupSupported && policy.allowCleanup ? Qt::Checked : Qt::Unchecked);
    if (!cleanupSupported) {
      cleanup->setFlags(cleanup->flags() & ~Qt::ItemIsEnabled);
      cleanup->setToolTip(tr("Disabled until a capability test confirms torrent listing and adapter removal-API support. Server-side permission is not destructively tested."));
    }
    m_clients->setItem(row, 0, use); m_clients->setItem(row, 1, name);
    m_clients->setRowHeight(row, clientRowHeight);
    m_clients->setItem(row, 2, new QTableWidgetItem(QString::number(policy.maxActiveDownloads)));
    m_clients->setItem(row, 3, new QTableWidgetItem(QString::number(policy.maxManagedTorrents)));
    m_clients->setItem(row, 4, new QTableWidgetItem(QString::number(policy.priority)));
    m_clients->setItem(row, 5, new QTableWidgetItem(QString::number(policy.minimumFreeBytes / GiB, 'f', 1)));
    m_clients->setItem(row, 6, new QTableWidgetItem(QString::number(policy.targetFreePercent, 'f', 1)));
    m_clients->setItem(row, 7, new QTableWidgetItem(QString::number(policy.configuredCapacityBytes / GiB, 'f', 1)));
    m_clients->setItem(row, 8, new QTableWidgetItem(QString::number(policy.maximumDownloadBytesPerSecond / (1024.0 * 1024.0), 'f', 1)));
    m_clients->setItem(row, 9, new QTableWidgetItem(QString::number(policy.requestTimeoutSeconds)));
    m_clients->setItem(row, 10, new QTableWidgetItem(QString::number(policy.retryAttempts)));
    m_clients->setItem(row, 11, cleanup);
    QString storageSource;
    if (client.capabilityFreeSpace) storageSource = policy.configuredCapacityBytes > 0
      ? tr("Live free + known total") : tr("Live free; total unknown");
    else if (m_config.reconciliationEnabled && client.capabilityTorrentList && policy.configuredCapacityBytes > 0)
      storageSource = tr("Reconciled estimate");
    else if (policy.configuredCapacityBytes > 0) storageSource = tr("Managed estimate");
    else storageSource = tr("Unknown");
    auto* storage = new QTableWidgetItem(storageSource);
    storage->setFlags(storage->flags() & ~Qt::ItemIsEditable);
    m_clients->setItem(row, 12, storage);
    for (int column = 0; column < m_clients->columnCount(); ++column) {
      if (column != 1 && m_clients->item(row, column)->toolTip().isEmpty())
        m_clients->item(row, column)->setToolTip(m_clients->horizontalHeaderItem(column)->toolTip());
    }
  }
  m_clients->blockSignals(false);
  if (!m_clientConfigs.isEmpty() && m_clients->currentRow() < 0) m_clients->setCurrentCell(0, 1);
  updateCapabilityDisplay();
}

void SettingsTorrentAutomation::refreshRules() {
  m_rules->blockSignals(true);
  m_rules->clear();
  for (const TorrentAutomationRule& rule : std::as_const(m_config.rules)) {
    auto* item = new QListWidgetItem(rule.name.isEmpty() ? tr("Unnamed rule") : rule.name, m_rules);
    item->setCheckState(rule.enabled ? Qt::Checked : Qt::Unchecked);
    QStringList conditions;
    if (!rule.titleRegularExpression.isEmpty()) conditions.append(tr("Title pattern: %1").arg(rule.titleRegularExpression));
    if (rule.minimumSizeBytes > 0) conditions.append(tr("Minimum: %1 GB").arg(rule.minimumSizeBytes / GiB, 0, 'f', 2));
    if (rule.maximumSizeBytes > 0) conditions.append(tr("Maximum: %1 GB").arg(rule.maximumSizeBytes / GiB, 0, 'f', 2));
    item->setToolTip(conditions.isEmpty() ? tr("All items matching the selected feeds and text filters")
                                          : conditions.join(QLatin1Char('\n')));
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
  auto* allFeeds = new QCheckBox(tr("Apply this rule to all feeds"), &dialog);
  allFeeds->setChecked(initial.feedIds.isEmpty());
  auto* feeds = new QListWidget(&dialog);
  feeds->setMinimumHeight(130);
  const QList<Feed*> availableFeeds = qApp->feedReader()->feedsModel()->feedsForIndex();
  for (const Feed* feed : availableFeeds) {
    auto* item = new QListWidgetItem(feed->fullIcon(), feed->title(), feeds);
    item->setData(Qt::UserRole, feed->customId());
    item->setCheckState(initial.feedIds.contains(feed->customId()) ? Qt::Checked : Qt::Unchecked);
    item->setToolTip(tr("Feed: %1\nInternal ID: %2").arg(feed->title(), feed->customId()));
  }
  feeds->setEnabled(!allFeeds->isChecked());
  connect(allFeeds, &QCheckBox::toggled, feeds, &QListWidget::setDisabled);
  auto* required = new QLineEdit(initial.requiredText, &dialog);
  auto* excluded = new QLineEdit(initial.excludedText, &dialog);
  auto* regex = new QLineEdit(initial.titleRegularExpression, &dialog);
  regex->setPlaceholderText(tr("Optional regular expression matched against the article title"));
  auto* minimumSize = new QDoubleSpinBox(&dialog);
  minimumSize->setRange(0.0, 1000000.0); minimumSize->setDecimals(2); minimumSize->setSuffix(tr(" GB"));
  minimumSize->setSpecialValueText(tr("No minimum")); minimumSize->setValue(initial.minimumSizeBytes / GiB);
  auto* maximumSize = new QDoubleSpinBox(&dialog);
  maximumSize->setRange(0.0, 1000000.0); maximumSize->setDecimals(2); maximumSize->setSuffix(tr(" GB"));
  maximumSize->setSpecialValueText(tr("No maximum")); maximumSize->setValue(initial.maximumSizeBytes / GiB);
  auto* clientList = new QListWidget(&dialog);
  for (const TorrentClientConfig& client : TorrentClientConfig::enabledInPriorityOrder(TorrentClientConfig::load(settings()))) {
    auto* item = new QListWidgetItem(client.name, clientList);
    item->setData(Qt::UserRole, client.id);
    item->setCheckState(initial.clientIds.isEmpty() || initial.clientIds.contains(client.id) ? Qt::Checked : Qt::Unchecked);
  }
  enabled->setToolTip(tr("Disable this rule temporarily without deleting it."));
  name->setToolTip(tr("A descriptive name shown in the RSS rules list and activity information."));
  allFeeds->setToolTip(tr("When checked, feed selection is ignored and the rule can match items from any feed."));
  feeds->setToolTip(tr("Select one or more feeds already added to RSS Guard. Their internal IDs are stored automatically."));
  required->setToolTip(tr("Case-insensitive text that must appear in the article title or content. Leave empty for no required text."));
  excluded->setToolTip(tr("Reject an article when this case-insensitive text appears in its title or content."));
  regex->setToolTip(tr("Optional case-insensitive regular expression matched against the article title. Invalid patterns never match."));
  minimumSize->setToolTip(tr("Only match torrents at least this large. Zero disables the minimum. Unknown sizes use the configured assumed size."));
  maximumSize->setToolTip(tr("Only match torrents no larger than this value. Zero disables the maximum. Unknown sizes use the configured assumed size."));
  clientList->setToolTip(tr("Select which torrent clients this rule may use. If all are selected, any participating eligible client may be chosen."));
  form->addRow(enabled); form->addRow(tr("Rule name:"), name); form->addRow(allFeeds); form->addRow(tr("Selected feeds:"), feeds);
  form->addRow(tr("Title/content must contain:"), required); form->addRow(tr("Must not contain:"), excluded);
  form->addRow(tr("Title pattern:"), regex); form->addRow(tr("Minimum torrent size:"), minimumSize);
  form->addRow(tr("Maximum torrent size:"), maximumSize); form->addRow(tr("Allowed clients:"), clientList);
  layout->addLayout(form);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog,
          [&dialog, name, allFeeds, feeds, regex, minimumSize, maximumSize]() {
    if (name->text().trimmed().isEmpty()) {
      QMessageBox::warning(&dialog, tr("Incomplete automation rule"), tr("Enter a rule name.")); return;
    }
    bool selectedFeed = allFeeds->isChecked();
    for (int i = 0; i < feeds->count() && !selectedFeed; ++i)
      selectedFeed = feeds->item(i)->checkState() == Qt::Checked;
    if (!selectedFeed) {
      QMessageBox::warning(&dialog, tr("Incomplete automation rule"),
                           tr("Select at least one feed or enable Apply this rule to all feeds.")); return;
    }
    const QRegularExpression expression(regex->text());
    if (!regex->text().trimmed().isEmpty() && !expression.isValid()) {
      QMessageBox::warning(&dialog, tr("Invalid title pattern"), expression.errorString()); return;
    }
    if (maximumSize->value() > 0.0 && minimumSize->value() > maximumSize->value()) {
      QMessageBox::warning(&dialog, tr("Invalid torrent size range"),
                           tr("The maximum torrent size must be zero or at least the minimum size.")); return;
    }
    dialog.accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  *accepted = dialog.exec() == QDialog::Accepted;
  if (!*accepted) return initial;
  TorrentAutomationRule result = initial;
  if (result.id.isEmpty()) result.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  result.enabled = enabled->isChecked(); result.name = name->text().trimmed();
  result.feedIds.clear();
  if (!allFeeds->isChecked()) {
    for (int i = 0; i < feeds->count(); ++i)
      if (feeds->item(i)->checkState() == Qt::Checked) result.feedIds.append(feeds->item(i)->data(Qt::UserRole).toString());
  }
  result.requiredText = required->text().trimmed(); result.excludedText = excluded->text().trimmed();
  result.titleRegularExpression = regex->text().trimmed();
  result.minimumSizeBytes = qint64(minimumSize->value() * GiB);
  result.maximumSizeBytes = qint64(maximumSize->value() * GiB);
  result.clientIds.clear();
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

void SettingsTorrentAutomation::duplicateRule() {
  const int row = m_rules->currentRow(); if (row < 0 || row >= m_config.rules.size()) return;
  TorrentAutomationRule copy = m_config.rules.at(row);
  copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  copy.name = tr("%1 (copy)").arg(copy.name);
  m_config.rules.insert(row + 1, copy);
  refreshRules(); m_rules->setCurrentRow(row + 1); dirtifySettings();
}

void SettingsTorrentAutomation::moveRuleUp() {
  const int row = m_rules->currentRow(); if (row <= 0 || row >= m_config.rules.size()) return;
  m_config.rules.swapItemsAt(row, row - 1);
  refreshRules(); m_rules->setCurrentRow(row - 1); dirtifySettings();
}

void SettingsTorrentAutomation::moveRuleDown() {
  const int row = m_rules->currentRow(); if (row < 0 || row + 1 >= m_config.rules.size()) return;
  m_config.rules.swapItemsAt(row, row + 1);
  refreshRules(); m_rules->setCurrentRow(row + 1); dirtifySettings();
}

void SettingsTorrentAutomation::removeRule() {
  const int row = m_rules->currentRow(); if (row < 0) return;
  if (QMessageBox::question(this, tr("Remove automation rule"), tr("Remove “%1”?").arg(m_config.rules.at(row).name)) != QMessageBox::Yes) return;
  m_config.rules.removeAt(row); refreshRules(); dirtifySettings();
}

void SettingsTorrentAutomation::refreshActivity() {
  m_activity->clear();
  TorrentAutomationEngine* engine = TorrentAutomationEngine::instance(qApp);
  m_activity->addItems(engine->recentActivity());
  if (m_activity->count() == 0) m_activity->addItem(tr("No automation activity has been recorded yet."));
  m_queue->clear();
  m_queue->addItems(engine->pendingRetries());
  if (m_queue->count() == 0) m_queue->addItem(tr("No queued automation items."));
}

void SettingsTorrentAutomation::runDryTest() {
  TorrentAutomationEngine::instance(qApp)->runDryTest();
}

void SettingsTorrentAutomation::runReadinessAudit() {
  saveSettings();
  m_config = TorrentAutomationConfig::load(settings());
  const QList<TorrentClientConfig> allClients = TorrentClientConfig::load(settings());
  QStringList errors, warnings, passed, participatingIds;
  for (const TorrentClientConfig& client : allClients) {
    const TorrentAutomationClientPolicy policy = m_config.policyFor(client.id);
    if (!client.enabled || !policy.enabled) continue;
    participatingIds.append(client.id);
    if (!client.capabilityTested || !client.capabilityConnected)
      errors.append(tr("%1 has not passed a capability test.").arg(client.name));
    else {
      passed.append(tr("%1 has a saved successful capability test.").arg(client.name));
      if (client.capabilityTestedAt.isValid() && client.capabilityTestedAt.daysTo(QDateTime::currentDateTimeUtc()) > 30)
        warnings.append(tr("%1's capability test is more than 30 days old.").arg(client.name));
    }
    if (!client.capabilityFreeSpace && policy.configuredCapacityBytes <= 0)
      warnings.append(tr("%1 has neither live disk space nor a configured fallback capacity.").arg(client.name));
    if (policy.allowCleanup && (!client.capabilityTorrentList || !client.capabilityRemoval))
      errors.append(tr("%1 permits cleanup without confirmed listing and removal capabilities.").arg(client.name));
  }
  if (participatingIds.isEmpty()) errors.append(tr("No enabled torrent client participates in automation."));

  int enabledRules = 0;
  for (const TorrentAutomationRule& rule : std::as_const(m_config.rules)) {
    if (!rule.enabled) continue;
    ++enabledRules;
    const QRegularExpression expression(rule.titleRegularExpression);
    if (!rule.titleRegularExpression.isEmpty() && !expression.isValid())
      errors.append(tr("Rule “%1” contains an invalid title pattern.").arg(rule.name));
    if (rule.maximumSizeBytes > 0 && rule.minimumSizeBytes > rule.maximumSizeBytes)
      errors.append(tr("Rule “%1” has an invalid torrent-size range.").arg(rule.name));
    for (const QString& clientId : rule.clientIds)
      if (!participatingIds.contains(clientId))
        warnings.append(tr("Rule “%1” references a client that is not currently participating.").arg(rule.name));
  }
  if (m_config.rules.isEmpty()) warnings.append(tr("No RSS rules are configured, so every recognised torrent item is eligible."));
  else if (enabledRules == 0) errors.append(tr("All RSS rules are disabled; no unattended item can match."));

  if (m_config.dryRun) passed.append(tr("Dry run is enabled, so sends and cleanup changes are simulated."));
  else warnings.append(tr("Dry run is disabled; matching RSS items can be sent immediately."));
  if (m_config.cleanupEnabled) {
    if (m_config.deleteData) warnings.append(tr("Cleanup can permanently delete downloaded data."));
    if (!m_config.cleanupRequireConfirmation) warnings.append(tr("Per-removal confirmation is disabled."));
    if (!m_config.cleanupGraceEnabled) warnings.append(tr("The cleanup grace period is disabled."));
    if (!m_config.maximumRemovalsEnabled) warnings.append(tr("The user-defined removal limit is disabled; the internal limit of 25 still applies."));
    if (!m_config.minimumSeedHoursEnabled && !m_config.minimumRatioEnabled && !m_config.minimumInactiveHoursEnabled)
      warnings.append(tr("All age, ratio and inactivity cleanup filters are disabled."));
  }
  const int queued = TorrentAutomationEngine::instance(qApp)->pendingRetries().size();
  if (queued > 0) warnings.append(tr("%1 automation item(s) are currently queued.").arg(queued));

  QMessageBox report(this);
  report.setWindowTitle(tr("Torrent automation readiness"));
  report.setIcon(errors.isEmpty() ? (warnings.isEmpty() ? QMessageBox::Information : QMessageBox::Warning)
                                  : QMessageBox::Critical);
  report.setText(errors.isEmpty() ? (warnings.isEmpty() ? tr("Ready for live automation.")
                                                        : tr("Usable, but review the warnings before going live."))
                                  : tr("Not ready for unattended live automation."));
  QStringList detail;
  if (!errors.isEmpty()) detail << tr("BLOCKING ISSUES:") << errors;
  if (!warnings.isEmpty()) detail << QString() << tr("WARNINGS:") << warnings;
  if (!passed.isEmpty()) detail << QString() << tr("PASSED:") << passed;
  report.setDetailedText(detail.join(QLatin1Char('\n')));
  report.exec();
}

void SettingsTorrentAutomation::exportConfiguration() {
  saveSettings();
  const QString path = QFileDialog::getSaveFileName(this, tr("Export torrent configuration"),
                                                     QStringLiteral("rssguard-torrent-configuration.json"),
                                                     tr("JSON files (*.json)"));
  if (path.isEmpty()) return;
  QJsonArray clients;
  for (const TorrentClientConfig& client : TorrentClientConfig::load(settings())) {
    clients.append(QJsonObject{{QStringLiteral("id"), client.id},
                               {QStringLiteral("name"), client.name},
                               {QStringLiteral("type"), static_cast<int>(client.type)},
                               {QStringLiteral("baseUrl"), client.baseUrl},
                               {QStringLiteral("buttonColor"), client.buttonColor},
                               {QStringLiteral("colorNotificationButtons"), client.colorNotificationButtons},
                               {QStringLiteral("colorContextMenus"), client.colorContextMenus},
                               {QStringLiteral("colorSettingsLists"), client.colorSettingsLists},
                               {QStringLiteral("enabled"), client.enabled},
                               {QStringLiteral("priority"), client.priority},
                               {QStringLiteral("useRssGuardProxy"), client.useRssGuardProxy},
                               {QStringLiteral("isDefault"), client.isDefault},
                               {QStringLiteral("savePath"), client.savePath},
                               {QStringLiteral("category"), client.category},
                               {QStringLiteral("tags"), QJsonArray::fromStringList(client.tags)}});
  }
  const QJsonObject automation = QJsonDocument::fromJson(
    settings()->value(QStringLiteral("TorrentAutomation"), QStringLiteral("configuration")).toByteArray()).object();
  const QJsonObject root{{QStringLiteral("format"), QStringLiteral("rssguard-torrent-configuration-v1")},
                         {QStringLiteral("exportedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                         {QStringLiteral("credentialsIncluded"), false},
                         {QStringLiteral("clients"), clients},
                         {QStringLiteral("automation"), automation}};
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
    QMessageBox::warning(this, tr("Export failed"), tr("The configuration file could not be written."));
    return;
  }
  QMessageBox::information(this, tr("Configuration exported"),
                           tr("Torrent settings were exported. Usernames, passwords and API tokens were not included."));
}

void SettingsTorrentAutomation::importConfiguration() {
  const QString path = QFileDialog::getOpenFileName(this, tr("Import torrent configuration"), QString(),
                                                     tr("JSON files (*.json)"));
  if (path.isEmpty()) return;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, tr("Import failed"), tr("The configuration file could not be read."));
    return;
  }
  QJsonParseError parseError;
  const QJsonObject root = QJsonDocument::fromJson(file.readAll(), &parseError).object();
  if (parseError.error != QJsonParseError::NoError ||
      root.value(QStringLiteral("format")).toString() != QStringLiteral("rssguard-torrent-configuration-v1") ||
      !root.value(QStringLiteral("automation")).isObject()) {
    QMessageBox::warning(this, tr("Import failed"), tr("This is not a supported RSS Guard torrent-configuration file."));
    return;
  }
  if (QMessageBox::warning(this, tr("Replace torrent configuration"),
      tr("Replace the current torrent-client layout and automation rules with this file? Existing credentials are retained only for clients with matching IDs."),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;

  const QList<TorrentClientConfig> existing = TorrentClientConfig::load(settings());
  QList<TorrentClientConfig> imported;
  for (const QJsonValue& value : root.value(QStringLiteral("clients")).toArray()) {
    const QJsonObject object = value.toObject();
    TorrentClientConfig client;
    client.id = object.value(QStringLiteral("id")).toString();
    for (const TorrentClientConfig& current : existing)
      if (current.id == client.id) { client = current; break; }
    client.id = object.value(QStringLiteral("id")).toString();
    client.name = object.value(QStringLiteral("name")).toString();
    client.type = static_cast<TorrentClientType>(qBound(0, object.value(QStringLiteral("type")).toInt(), 6));
    client.baseUrl = object.value(QStringLiteral("baseUrl")).toString();
    client.buttonColor = object.value(QStringLiteral("buttonColor")).toString();
    client.colorNotificationButtons = object.value(QStringLiteral("colorNotificationButtons")).toBool(true);
    client.colorContextMenus = object.value(QStringLiteral("colorContextMenus")).toBool(true);
    client.colorSettingsLists = object.value(QStringLiteral("colorSettingsLists")).toBool(true);
    client.enabled = object.value(QStringLiteral("enabled")).toBool(true);
    client.priority = object.value(QStringLiteral("priority")).toInt(1);
    client.useRssGuardProxy = object.value(QStringLiteral("useRssGuardProxy")).toBool(true);
    client.isDefault = object.value(QStringLiteral("isDefault")).toBool(false);
    client.savePath = object.value(QStringLiteral("savePath")).toString();
    client.category = object.value(QStringLiteral("category")).toString();
    client.tags.clear();
    for (const QJsonValue& tag : object.value(QStringLiteral("tags")).toArray()) client.tags.append(tag.toString());
    if (!client.id.isEmpty() && !client.name.isEmpty() && !client.baseUrl.isEmpty()) imported.append(client);
  }
  TorrentClientConfig::save(settings(), imported);
  settings()->setValue(QStringLiteral("TorrentAutomation"), QStringLiteral("configuration"),
                       QJsonDocument(root.value(QStringLiteral("automation")).toObject()).toJson(QJsonDocument::Compact));
  loadSettings();
  dirtifySettings();
  QMessageBox::information(this, tr("Configuration imported"),
                           tr("Configuration imported. Enter credentials for any newly imported clients before testing them."));
}

void SettingsTorrentAutomation::retryQueuedItem() {
  if (m_queue->currentRow() < 0 || TorrentAutomationEngine::instance(qApp)->pendingRetries().isEmpty()) return;
  TorrentAutomationEngine::instance(qApp)->retryPending(m_queue->currentRow());
  refreshActivity();
}

void SettingsTorrentAutomation::retryAllQueuedItems() {
  TorrentAutomationEngine* engine = TorrentAutomationEngine::instance(qApp);
  if (engine->pendingRetries().isEmpty()) return;
  if (QMessageBox::question(this, tr("Retry all queued automation"),
                            tr("Retry every queued item now? Normal client limits and safety checks still apply.")) != QMessageBox::Yes) return;
  engine->retryAllPending(); refreshActivity();
}

void SettingsTorrentAutomation::chooseQueuedClient() {
  if (m_queue->currentRow() < 0 || TorrentAutomationEngine::instance(qApp)->pendingRetries().isEmpty()) return;
  const QList<TorrentClientConfig> clients = TorrentClientConfig::enabledInPriorityOrder(TorrentClientConfig::load(settings()));
  QStringList names;
  for (const TorrentClientConfig& client : clients) names.append(client.name);
  if (names.isEmpty()) return;
  bool accepted = false;
  const QString chosen = QInputDialog::getItem(this, tr("Choose torrent client"), tr("Send queued item to:"),
                                                names, 0, false, &accepted);
  if (!accepted) return;
  const int selected = names.indexOf(chosen);
  if (selected < 0) return;
  TorrentAutomationEngine::instance(qApp)->sendPendingToClient(m_queue->currentRow(), clients.at(selected).id);
  refreshActivity();
}

void SettingsTorrentAutomation::cancelQueuedItem() {
  if (m_queue->currentRow() < 0 || TorrentAutomationEngine::instance(qApp)->pendingRetries().isEmpty()) return;
  if (QMessageBox::question(this, tr("Cancel queued automation"),
                            tr("Remove the selected item from the retry queue? The torrent itself will not be changed.")) != QMessageBox::Yes) return;
  TorrentAutomationEngine::instance(qApp)->cancelPending(m_queue->currentRow());
  refreshActivity();
}

void SettingsTorrentAutomation::cancelAllQueuedItems() {
  TorrentAutomationEngine* engine = TorrentAutomationEngine::instance(qApp);
  if (engine->pendingRetries().isEmpty()) return;
  if (QMessageBox::warning(this, tr("Cancel all queued automation"),
                           tr("Remove every item from the persistent automation queue? Existing torrents are not changed."),
                           QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
  engine->cancelAllPending(); refreshActivity();
}

void SettingsTorrentAutomation::exportActivity() {
  const QString path = QFileDialog::getSaveFileName(this, tr("Export torrent automation activity"),
                                                     QStringLiteral("rssguard-torrent-activity.json"),
                                                     tr("JSON files (*.json)"));
  if (path.isEmpty()) return;
  TorrentAutomationEngine* engine = TorrentAutomationEngine::instance(qApp);
  const QJsonObject root{{QStringLiteral("format"), QStringLiteral("rssguard-torrent-activity-v1")},
                         {QStringLiteral("exportedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                         {QStringLiteral("history"), engine->activityHistory()},
                         {QStringLiteral("pendingQueue"), QJsonArray::fromStringList(engine->pendingRetries())}};
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
    QMessageBox::warning(this, tr("Activity export failed"), tr("The activity file could not be written.")); return;
  }
  QMessageBox::information(this, tr("Activity exported"),
                           tr("The report contains automation decisions and may include torrent titles, URLs and client identifiers. It contains no stored passwords or tokens."));
}

void SettingsTorrentAutomation::clearActivity() {
  if (TorrentAutomationEngine::instance(qApp)->activityHistory().isEmpty()) return;
  if (QMessageBox::question(this, tr("Clear torrent automation activity"),
                            tr("Permanently clear the retained decision history? Processed-item protection, managed allocations and the pending queue are not changed.")) != QMessageBox::Yes) return;
  TorrentAutomationEngine::instance(qApp)->clearActivityHistory(); refreshActivity();
}

void SettingsTorrentAutomation::updateCleanupControls() {
  const bool enabled = m_cleanup->isChecked();
  const QList<QWidget*> cleanup_widgets = {m_deleteData, m_confirmCleanup, m_seedHoursEnabled, m_ratioEnabled,
                                            m_inactiveHoursEnabled, m_maxRemovalsEnabled, m_cleanupStopGbEnabled,
                                            m_protectUploading, m_protectUnknownSpeed, m_protectRecentHours,
                                            m_cleanupGrace, m_smartCleanup, m_minimumCopiesEnabled,
                                            m_protectedTags, m_protectedTrackers, m_cleanupSchedule,
                                            m_cleanupBatchPercent};
  for (QWidget* widget : cleanup_widgets) widget->setEnabled(enabled);
  m_seedHours->setEnabled(enabled && m_seedHoursEnabled->isChecked());
  m_ratio->setEnabled(enabled && m_ratioEnabled->isChecked());
  m_inactiveHours->setEnabled(enabled && m_inactiveHoursEnabled->isChecked());
  m_maxRemovals->setEnabled(enabled && m_maxRemovalsEnabled->isChecked());
  m_cleanupStopGb->setEnabled(enabled && m_cleanupStopGbEnabled->isChecked());
  m_protectUploadKib->setEnabled(enabled && m_protectUploading->isChecked());
  m_cleanupGraceHours->setEnabled(enabled && m_cleanupGrace->isChecked());
  m_minimumCopies->setEnabled(enabled && m_minimumCopiesEnabled->isChecked());
  m_cleanupScheduleStart->setEnabled(enabled && m_cleanupSchedule->isChecked());
  m_cleanupScheduleEnd->setEnabled(enabled && m_cleanupSchedule->isChecked());
}

void SettingsTorrentAutomation::updateCapabilityDisplay() {
  const int row = m_clients == nullptr ? -1 : m_clients->currentRow();
  const bool selected = row >= 0 && row < m_clientConfigs.size();
  const TorrentClientConfig config = selected ? m_clientConfigs.at(row) : TorrentClientConfig();
  m_capConnected->setChecked(selected && config.capabilityConnected);
  m_capStatus->setChecked(selected && config.capabilityLiveStatus);
  m_capSpace->setChecked(selected && config.capabilityFreeSpace);
  m_capList->setChecked(selected && config.capabilityTorrentList);
  m_capRates->setChecked(selected && config.capabilityTransferRates);
  m_capRemoval->setChecked(selected && config.capabilityRemoval);
  m_testSelected->setEnabled(selected);
  if (!selected) m_capabilityTested->setText(tr("Select a client to view or test its capabilities."));
  else if (!config.capabilityTested)
    m_capabilityTested->setText(tr("Not tested yet. Testing is non-destructive: it never adds or removes a torrent."));
  else m_capabilityTested->setText(tr("Last tested: %1 — %2")
    .arg(QLocale().toString(config.capabilityTestedAt.toLocalTime(), QLocale::ShortFormat), config.capabilityDetail));
}

void SettingsTorrentAutomation::storeCapabilityResult(const TorrentClientConfig& tested,
                                                       bool connected,
                                                       bool liveStatus,
                                                       bool freeSpace,
                                                       bool torrentList,
                                                       bool transferRates,
                                                       bool removal,
                                                       qint64 totalBytes,
                                                       const QString& detail) {
  QList<TorrentClientConfig> allClients = TorrentClientConfig::load(settings());
  for (TorrentClientConfig& config : allClients) {
    if (config.id != tested.id) continue;
    config.capabilityTested = true;
    config.capabilityConnected = connected;
    config.capabilityLiveStatus = liveStatus;
    config.capabilityFreeSpace = freeSpace;
    config.capabilityTorrentList = torrentList;
    config.capabilityTransferRates = transferRates;
    config.capabilityRemoval = removal;
    config.capabilityTestedAt = QDateTime::currentDateTimeUtc();
    config.capabilityDetail = detail;
  }
  TorrentClientConfig::save(settings(), allClients);
  if (totalBytes > 0) {
    bool found = false;
    for (TorrentAutomationClientPolicy& policy : m_config.clients) {
      if (policy.clientId != tested.id) continue;
      policy.configuredCapacityBytes = totalBytes;
      found = true;
      break;
    }
    if (!found) {
      TorrentAutomationClientPolicy policy = m_config.policyFor(tested.id);
      policy.clientId = tested.id;
      policy.configuredCapacityBytes = totalBytes;
      m_config.clients.append(policy);
    }
    dirtifySettings();
  }
  for (TorrentClientConfig& config : m_clientConfigs) {
    if (config.id != tested.id) continue;
    config.capabilityTested = true;
    config.capabilityConnected = connected;
    config.capabilityLiveStatus = liveStatus;
    config.capabilityFreeSpace = freeSpace;
    config.capabilityTorrentList = torrentList;
    config.capabilityTransferRates = transferRates;
    config.capabilityRemoval = removal;
    config.capabilityTestedAt = QDateTime::currentDateTimeUtc();
    config.capabilityDetail = detail;
  }
  updateCapabilityDisplay();
}

void SettingsTorrentAutomation::testSelectedClient() {
  const int row = m_clients->currentRow();
  if (row < 0 || row >= m_clientConfigs.size()) return;
  TorrentClientConfig config = m_clientConfigs.at(row);
  const TorrentAutomationClientPolicy policy = m_config.policyFor(config.id);
  config.requestTimeoutSeconds = policy.requestTimeoutSeconds > 0
                                   ? policy.requestTimeoutSeconds : m_config.requestTimeoutSeconds;
  const int maximumRetries = policy.retryAttempts >= 0 ? policy.retryAttempts : m_config.retryAttempts;
  m_testSelected->setEnabled(false); m_testAll->setEnabled(false);
  TorrentClient* client = TorrentClient::create(config, this);
  auto connectionAttempts = std::make_shared<int>(0);
  connect(client, &TorrentClient::testFinished, this, [this, client, config, maximumRetries, connectionAttempts](bool success, const QString& message) {
    if (!success && m_config.retryEnabled && *connectionAttempts < maximumRetries && transientStatusFailure(message)) {
      qint64 delay = qMax(1, m_config.retryInitialSeconds);
      if (m_config.retryExponentialBackoff) delay *= (1LL << qMin(*connectionAttempts, 16));
      delay = qMin<qint64>(delay, qMax(1, m_config.retryMaximumSeconds));
      ++*connectionAttempts;
      QTimer::singleShot(int(delay * 1000), client, [client]() { client->testConnection(); });
      return;
    }
    if (!success || !client->supportsLiveStatus()) {
      storeCapabilityResult(config, success, false, false, false, false, false, -1, message);
      QMessageBox::information(this, tr("Automation capability test"),
        success ? tr("Connected successfully. This adapter can send torrents, but live workload, disk-space, listing and removal-API monitoring are not available.") : message);
      client->deleteLater(); m_testAll->setEnabled(true); refreshClientPolicies(); return;
    }
    auto attempts = std::make_shared<int>(0);
    connect(client, &TorrentClient::statusFinished, this, [this, client, config, attempts, maximumRetries](const TorrentClientStatus& status) {
      if (!status.reachable && m_config.retryEnabled && *attempts < maximumRetries &&
          transientStatusFailure(status.detail)) {
        qint64 delay = qMax(1, m_config.retryInitialSeconds);
        if (m_config.retryExponentialBackoff) delay *= (1LL << qMin(*attempts, 16));
        delay = qMin<qint64>(delay, qMax(1, m_config.retryMaximumSeconds));
        ++*attempts;
        QTimer::singleShot(int(delay * 1000), client, [client]() { client->fetchStatus(); });
        return;
      }
      const bool live = status.reachable;
      const bool removal = live && client->supportsRemoval();
      const bool transferRates = live && hasTransferRateValues(status);
      storeCapabilityResult(config, true, live, live && status.liveSpace, live, transferRates,
                            removal, status.totalBytes, capabilitySummary(config, status, transferRates, removal));
      QMessageBox::information(this, tr("Automation capability test"),
        live ? tr("Capability test completed. The detected features are shown as ticks under Clients and limits.") : status.detail);
      client->deleteLater(); m_testAll->setEnabled(true); refreshClientPolicies();
    });
    client->fetchStatus();
  });
  client->testConnection();
}

void SettingsTorrentAutomation::testAllClients() {
  m_testQueue = m_clientConfigs; m_testResults.clear(); m_testFailures = 0;
  if (m_testQueue.isEmpty()) {
    QMessageBox::information(this, tr("Automation capability tests"), tr("There are no enabled clients to test.")); return;
  }
  m_testSelected->setEnabled(false); m_testAll->setEnabled(false); testNextClient();
}

void SettingsTorrentAutomation::testNextClient() {
  if (m_testQueue.isEmpty()) {
    QMessageBox result(this);
    result.setWindowTitle(tr("Automation capability tests"));
    result.setIcon(m_testFailures == 0 ? QMessageBox::Information : QMessageBox::Warning);
    result.setText(tr("Tested %1 client(s): %2 passed, %3 failed.")
      .arg(m_testResults.size()).arg(m_testResults.size() - m_testFailures).arg(m_testFailures));
    result.setInformativeText(m_testResults.join(QStringLiteral("<br>"))); result.exec();
    m_testAll->setEnabled(true); refreshClientPolicies(); return;
  }
  TorrentClientConfig config = m_testQueue.takeFirst();
  const TorrentAutomationClientPolicy policy = m_config.policyFor(config.id);
  config.requestTimeoutSeconds = policy.requestTimeoutSeconds > 0
                                   ? policy.requestTimeoutSeconds : m_config.requestTimeoutSeconds;
  const int maximumRetries = policy.retryAttempts >= 0 ? policy.retryAttempts : m_config.retryAttempts;
  TorrentClient* client = TorrentClient::create(config, this);
  auto connectionAttempts = std::make_shared<int>(0);
  connect(client, &TorrentClient::testFinished, this, [this, client, config, maximumRetries, connectionAttempts](bool success, const QString& message) {
    if (!success && m_config.retryEnabled && *connectionAttempts < maximumRetries && transientStatusFailure(message)) {
      qint64 delay = qMax(1, m_config.retryInitialSeconds);
      if (m_config.retryExponentialBackoff) delay *= (1LL << qMin(*connectionAttempts, 16));
      delay = qMin<qint64>(delay, qMax(1, m_config.retryMaximumSeconds));
      ++*connectionAttempts;
      QTimer::singleShot(int(delay * 1000), client, [client]() { client->testConnection(); });
      return;
    }
    if (!success || !client->supportsLiveStatus()) {
      if (!success) ++m_testFailures;
      storeCapabilityResult(config, success, false, false, false, false, false, -1, message);
      m_testResults.append(tr("%1 %2 — %3").arg(success ? QStringLiteral("✓") : QStringLiteral("✗"), config.name, message));
      client->deleteLater(); testNextClient(); return;
    }
    auto attempts = std::make_shared<int>(0);
    connect(client, &TorrentClient::statusFinished, this, [this, client, config, attempts, maximumRetries](const TorrentClientStatus& status) {
      if (!status.reachable && m_config.retryEnabled && *attempts < maximumRetries &&
          transientStatusFailure(status.detail)) {
        qint64 delay = qMax(1, m_config.retryInitialSeconds);
        if (m_config.retryExponentialBackoff) delay *= (1LL << qMin(*attempts, 16));
        delay = qMin<qint64>(delay, qMax(1, m_config.retryMaximumSeconds));
        ++*attempts;
        QTimer::singleShot(int(delay * 1000), client, [client]() { client->fetchStatus(); });
        return;
      }
      if (!status.reachable) ++m_testFailures;
      const bool removal = status.reachable && client->supportsRemoval();
      const bool transferRates = status.reachable && hasTransferRateValues(status);
      storeCapabilityResult(config, true, status.reachable, status.reachable && status.liveSpace,
                            status.reachable, transferRates, removal, status.totalBytes,
                            capabilitySummary(config, status, transferRates, removal));
      m_testResults.append(tr("%1 %2 — %3").arg(status.reachable ? QStringLiteral("✓") : QStringLiteral("✗"),
                                                config.name, status.detail));
      client->deleteLater(); testNextClient();
    });
    client->fetchStatus();
  });
  client->testConnection();
}
