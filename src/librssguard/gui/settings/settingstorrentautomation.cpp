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
#include <QHash>
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
#include <QPalette>
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
#include <QWizard>
#include <QWizardPage>

#include <utility>
#include <memory>

namespace {
  constexpr double MiB = 1024.0 * 1024.0;
  constexpr double GiB = 1024.0 * 1024.0 * 1024.0;
  constexpr double TiB = 1024.0 * 1024.0 * 1024.0 * 1024.0;

  double byteUnitFactor(const QString& unit) {
    if (unit.compare(QStringLiteral("MiB"), Qt::CaseInsensitive) == 0 ||
        unit.compare(QStringLiteral("MB"), Qt::CaseInsensitive) == 0) return MiB;
    if (unit.compare(QStringLiteral("TiB"), Qt::CaseInsensitive) == 0 ||
        unit.compare(QStringLiteral("TB"), Qt::CaseInsensitive) == 0) return TiB;
    return GiB;
  }

  QString formatByteQuantity(qint64 bytes, const QString& unit) {
    const int decimals = unit == QStringLiteral("MiB") ? 0 : 2;
    QString value = QString::number(bytes / byteUnitFactor(unit), 'f', decimals);
    while (value.contains(QLatin1Char('.')) && value.endsWith(QLatin1Char('0'))) value.chop(1);
    if (value.endsWith(QLatin1Char('.'))) value.chop(1);
    return value;
  }

  qint64 parseByteQuantity(QString text, const QString& defaultUnit, bool* ok = nullptr) {
    static const QRegularExpression expression(
      QStringLiteral("^\\s*([+-]?[0-9]+(?:[\\.,][0-9]+)?)\\s*(MiB|MB|GiB|GB|TiB|TB)?(?:\\s*/?s)?\\s*$"),
      QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = expression.match(text);
    bool numberOk = false;
    const double number = match.hasMatch()
                            ? QString(match.captured(1)).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&numberOk)
                            : 0.0;
    if (ok != nullptr) *ok = numberOk && number >= 0.0;
    if (!numberOk || number < 0.0) return 0;
    const QString unit = match.captured(2).isEmpty() ? defaultUnit : match.captured(2);
    return qint64(number * byteUnitFactor(unit));
  }

  QString formatTransferRate(qint64 bytesPerSecond) {
    if (bytesPerSecond >= qint64(TiB)) return formatByteQuantity(bytesPerSecond, QStringLiteral("TiB")) + QStringLiteral(" TiB/s");
    if (bytesPerSecond >= qint64(GiB)) return formatByteQuantity(bytesPerSecond, QStringLiteral("GiB")) + QStringLiteral(" GiB/s");
    return formatByteQuantity(bytesPerSecond, QStringLiteral("MiB")) + QStringLiteral(" MiB/s");
  }

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

  enum QuickPreset {
    PresetCustom = 0,
    PresetSafeTest = 1,
    PresetBalanced = 2,
    PresetThirtyDay = 3,
    PresetQuickTurnaround = 4,
    PresetLongSeed = 5
  };

  QString quickPresetName(int preset) {
    switch (preset) {
      case PresetSafeTest: return QObject::tr("Safety-first test only");
      case PresetBalanced: return QObject::tr("Balanced protected automation");
      case PresetThirtyDay: return QObject::tr("30-day automatic rotation");
      case PresetQuickTurnaround: return QObject::tr("Quick turnaround — 3-day seed / 7-day limit");
      case PresetLongSeed: return QObject::tr("Long-term seeding");
      default: return QObject::tr("Choose a preset…");
    }
  }

  QString quickPresetDetail(int preset, bool richText = true) {
    QString title, does, keeps, risk;
    switch (preset) {
      case PresetSafeTest:
        title = QObject::tr("Safest way to learn and verify the feature");
        does = QObject::tr("Enables automation in Dry run, uses Balanced routing, enables retries, reconciliation, duplicate protection and client-health protection, and disables all cleanup.");
        keeps = QObject::tr("Your torrent clients, client limits, capacities and RSS matching rules are not replaced.");
        risk = QObject::tr("No torrent is sent or removed while Dry run remains on.");
        break;
      case PresetBalanced:
        title = QObject::tr("Common protected setup for normal automatic use");
        does = QObject::tr("Uses Balanced routing; requires 7 days completed, ratio 1.0 and 24 hours inactive before storage cleanup; protects active or recently uploading torrents; marks candidates for a 24-hour grace period; and removes at most one torrent per run. Fixed-age removal is off.");
        keeps = QObject::tr("It enables downloaded-data deletion so storage cleanup can genuinely recover space, but also enables confirmation and Dry run. Client-specific limits and RSS rules are preserved.");
        risk = QObject::tr("After you later disable Dry run, an approved eligible torrent and its downloaded files can be permanently deleted.");
        break;
      case PresetThirtyDay:
        title = QObject::tr("Predictable 30-day content rotation");
        does = QObject::tr("Enables storage cleanup plus a firm 720-hour (30-day) retention deadline. Expired completed managed torrents are due even when space is healthy or ratio/activity targets are not met. Only one removal is allowed per run.");
        keeps = QObject::tr("Protected tags, protected tracker text and minimum-copy protection still win. Confirmation and Dry run are enabled, and client-specific settings and RSS rules are preserved.");
        risk = QObject::tr("After Dry run is disabled, expired torrent jobs and their downloaded files can be permanently deleted.");
        break;
      case PresetQuickTurnaround:
        title = QObject::tr("Short seeding and faster space recovery for a steady flow of new releases");
        does = QObject::tr("Makes storage-pressure cleanup eligible after 72 hours completed, ratio 0.5 and 6 hours inactive; keeps cleaning until 60 GiB is free; uses a 6-hour grace period; and can remove up to three torrents per run. A firm 168-hour (7-day) deadline removes completed managed torrents even when space is healthy.");
        keeps = QObject::tr("Active and unknown uploads, uploads seen within 6 hours, protected tags/trackers and minimum-copy protection are retained before the deadline. Confirmation and Dry run start enabled. Clients, per-client capacities and RSS rules are preserved.");
        risk = QObject::tr("This is deliberately aggressive. A 0.5 ratio or 7-day firm deadline may be too short for private-tracker rules, bonus goals or slow swarms. At the deadline, upload and ratio delays no longer postpone removal. Once Dry run is disabled and a deletion is approved, the torrent job and downloaded files can be permanently deleted.");
        break;
      case PresetLongSeed:
        title = QObject::tr("Keep torrents seeding for longer");
        does = QObject::tr("Requires 30 days completed, ratio 2.0 and 72 hours inactive for storage cleanup. A non-firm 90-day retention trigger is enabled, so normal activity protections may postpone removal. It uses a 48-hour grace period and one removal per run.");
        keeps = QObject::tr("Active or unknown uploads, recent uploads, protected tags/trackers and configured copy protection are retained. Confirmation and Dry run are enabled.");
        risk = QObject::tr("Downloaded-data deletion is enabled; after Dry run is disabled, eligible files can be permanently deleted.");
        break;
      default:
        return QObject::tr("Select a preset to see every change and risk before applying it. Presets never replace torrent clients, per-client capacities or RSS rules.");
    }
    if (richText)
      return QObject::tr("<b>%1</b><br><br><b>What it sets:</b> %2<br><br><b>What it keeps:</b> %3<br><br><b>Risk:</b> %4<br><br><b>Safety:</b> Every preset starts with Dry run ON.")
        .arg(title, does, keeps, risk);
    return QObject::tr("%1\n\nWhat it sets: %2\n\nWhat it keeps: %3\n\nRisk: %4\n\nSafety: Every preset starts with Dry run ON.")
      .arg(title, does, keeps, risk);
  }

  QString informationCardStyle(const QPalette& palette, const QString& objectName) {
    const bool dark = palette.color(QPalette::Window).lightness() < 128 ||
                      palette.color(QPalette::Base).lightness() < 128;
    const QString background = dark ? QStringLiteral("#26364F") : QStringLiteral("#EAF2FF");
    const QString foreground = dark ? QStringLiteral("#F5F8FC") : QStringLiteral("#17243D");
    const QString border = dark ? QStringLiteral("#6F8FB8") : QStringLiteral("#9DB4D3");
    return QStringLiteral("QLabel#%1 { background: %2; color: %3; border: 1px solid %4; "
                          "border-radius: 8px; padding: 12px; }")
      .arg(objectName, background, foreground, border);
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

  auto* setupWizard = new QPushButton(tr("Start guided setup wizard"), this);
  setupWizard->setToolTip(tr("Configure every torrent-automation section step by step, with plain-language explanations and examples."));
  auto* wizardRow = new QHBoxLayout();
  wizardRow->addStretch();
  wizardRow->addWidget(setupWizard);
  outer->addLayout(wizardRow);

  auto* tabs = new QTabWidget(this);
  outer->addWidget(tabs, 1);

  auto* general = new QWidget(tabs);
  auto* generalLayout = new QVBoxLayout(general);
  auto* presetBox = new QGroupBox(tr("Quick Set — common starting configurations"), general);
  auto* presetLayout = new QVBoxLayout(presetBox);
  auto* presetRow = new QHBoxLayout();
  auto* presetChoice = new QComboBox(presetBox);
  for (int preset = PresetCustom; preset <= PresetLongSeed; ++preset)
    presetChoice->addItem(quickPresetName(preset), preset);
  auto* applyPreset = new QPushButton(tr("Review and apply preset"), presetBox);
  auto* presetDetail = new QLabel(quickPresetDetail(PresetCustom), presetBox);
  presetDetail->setWordWrap(true); presetDetail->setTextFormat(Qt::RichText);
  presetDetail->setObjectName(QStringLiteral("quickPresetDetail"));
  presetDetail->setStyleSheet(informationCardStyle(presetDetail->palette(), QStringLiteral("quickPresetDetail")));
  applyPreset->setEnabled(false);
  presetRow->addWidget(presetChoice, 1); presetRow->addWidget(applyPreset);
  presetLayout->addLayout(presetRow); presetLayout->addWidget(presetDetail);
  generalLayout->addWidget(presetBox);
  connect(presetChoice, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [presetChoice, presetDetail, applyPreset]() {
    const int preset = presetChoice->currentData().toInt();
    presetDetail->setText(quickPresetDetail(preset));
    applyPreset->setEnabled(preset != PresetCustom);
  });
  connect(applyPreset, &QPushButton::clicked, this, [this, presetChoice]() {
    const int preset = presetChoice->currentData().toInt();
    if (preset == PresetCustom) return;
    QMessageBox review(this);
    review.setWindowTitle(tr("Review Quick Set preset"));
    review.setIcon(preset == PresetSafeTest ? QMessageBox::Information : QMessageBox::Warning);
    review.setText(quickPresetName(preset));
    review.setInformativeText(quickPresetDetail(preset, false));
    auto* apply = review.addButton(tr("Apply this preset"), QMessageBox::AcceptRole);
    review.addButton(QMessageBox::Cancel);
    review.exec();
    if (review.clickedButton() != apply) return;
    applyQuickPreset(preset);
    QMessageBox::information(this, tr("Preset applied"),
      tr("The preset is now shown in the settings. Dry run is ON. Press Apply or OK to save, then run the dry test and readiness check before considering live mode."));
  });
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
  exportConfiguration->setToolTip(tr("Save every portable torrent setting: clients, usernames, destinations, colours, notification choice, limits, rules, routing, retries, schedules, storage, cleanup, retention and protections. Passwords and API tokens are excluded for safety."));
  importConfiguration->setToolTip(tr("Import every portable torrent setting from a supported JSON file. Existing matching passwords and tokens are retained locally; credentials are never read from the file."));
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
  auto* storageUnitRow = new QHBoxLayout();
  storageUnitRow->addWidget(new QLabel(tr("Storage display/input unit:"), clientsPage));
  m_storageUnit = new QComboBox(clientsPage);
  m_storageUnit->addItem(tr("MiB (mebibytes)"), QStringLiteral("MiB"));
  m_storageUnit->addItem(tr("GiB (gibibytes)"), QStringLiteral("GiB"));
  m_storageUnit->addItem(tr("TiB (tebibytes)"), QStringLiteral("TiB"));
  m_storageUnit->setToolTip(tr("Controls unsuffixed values in the storage columns. You can always type an explicit value such as 2048 MiB, 750 GiB or 1.8 TiB; RSS Guard converts it to bytes automatically."));
  storageUnitRow->addWidget(m_storageUnit);
  storageUnitRow->addWidget(new QLabel(tr("Explicit MiB/GiB/TiB suffixes override this selection."), clientsPage));
  storageUnitRow->addStretch();
  clientsLayout->addLayout(storageUnitRow);
  m_clients = new QTableWidget(clientsPage);
  m_clients->setColumnCount(13);
  m_clients->setHorizontalHeaderLabels({tr("Use"), tr("Client"), tr("Max active"), tr("Max managed"), tr("Priority"), tr("Min free GiB"), tr("Target free %"), tr("Capacity GiB"), tr("Max down rate"), tr("Timeout s"), tr("Retries"), tr("Cleanup"), tr("Storage source")});
  const QStringList clientTips{
    tr("Include this client in automatic routing."),
    tr("Configured torrent client. Its colour comes from Torrent clients settings."),
    tr("Do not send another torrent when this many downloads are active. Zero disables this limit."),
    tr("Maximum RSS Guard-managed torrents retained on this client. Zero disables this limit."),
    tr("Automation preference: 1 is highest priority. Used by Priority, Priority-biased and Balanced routing."),
    tr("Keep at least this much free space after routing a torrent. Zero disables the reserve. Accepts MiB, GiB or TiB suffixes."),
    tr("Keep at least this percentage of total capacity free after routing. Zero disables the percentage target."),
    tr("Fallback torrent-storage budget when the client API cannot report live disk space. RSS Guard subtracts every listed torrent, including manually added ones, plus pending managed reservations. Deduct space used by unrelated files before entering this value. Zero means unknown."),
    tr("Treat the client as overloaded above this total download speed. Zero disables this limit. Accepts MiB/s, GiB/s or TiB/s suffixes; unsuffixed values use MiB/s."),
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
  m_cleanup = new QCheckBox(tr("Allow automatic cleanup for storage pressure and/or maximum retention time"), cleanupPage);
  m_deleteData = new QCheckBox(tr("Delete downloaded data as well as the torrent"), cleanupPage);
  m_confirmCleanup = new QCheckBox(tr("Ask before every removal"), cleanupPage);
  m_retentionEnabled = new QCheckBox(tr("Remove completed managed torrents after a maximum time"), cleanupPage);
  m_retentionHours = new QSpinBox(cleanupPage); m_retentionHours->setRange(1, 100000); m_retentionHours->setSuffix(tr(" hours"));
  m_retentionStrict = new QCheckBox(tr("Treat the maximum time as a firm deadline"), cleanupPage);
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
  m_cleanup->setToolTip(tr("Master switch for storage-pressure cleanup and fixed maximum-retention cleanup. Only completed RSS Guard-managed torrents on opted-in clients are considered."));
  m_deleteData->setToolTip(tr("Also erase downloaded files. Leave off to remove only the torrent job. This action cannot be undone."));
  m_confirmCleanup->setToolTip(tr("Ask for approval before every removal. Recommended while validating your rules and limits."));
  m_retentionEnabled->setToolTip(tr("Run periodic cleanup even when no disk space is needed. A completed managed torrent becomes due after the selected time."));
  m_retentionHours->setToolTip(tr("Maximum time after completion before removal. If completion time is unavailable, the client-reported added time is used. Examples: 24 hours = 1 day, 168 = 7 days, 720 = 30 days."));
  m_retentionStrict->setToolTip(tr("When enabled, the deadline overrides ratio, inactivity and upload-activity protections. Protected tags, protected trackers and minimum-copy protection still win."));
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
  cleanupForm->addRow(m_retentionEnabled, m_retentionHours);
  cleanupForm->addRow(m_retentionStrict);
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

  auto* simplePage = new QWidget(tabs);
  auto* simpleLayout = new QVBoxLayout(simplePage);
  auto* simpleIntro = new QLabel(
    tr("<b>Simple dry-run results</b><br>Only final, user-relevant outcomes are shown here. "
       "Hover over a result—or use Activity—for the complete explanation."), simplePage);
  simpleIntro->setWordWrap(true);
  auto* simpleLegend = new QLabel(
    tr("<span style='color:#1976d2'><b>&#9679; WOULD SEND</b></span>&nbsp;&nbsp; "
       "<span style='color:#7b1fa2'><b>&#9679; WOULD DELETE</b></span>&nbsp;&nbsp; "
       "<span style='color:#2e7d32'><b>&#9679; WOULD KEEP</b></span>&nbsp;&nbsp; "
       "<span style='color:#c77700'><b>&#9679; WOULD WAIT / RETRY</b></span>&nbsp;&nbsp; "
       "<span style='color:#c62828'><b>&#9679; BLOCKED</b></span>&nbsp;&nbsp; "
       "<span style='color:#607d8b'><b>&#9679; SKIPPED</b></span>"), simplePage);
  simpleLegend->setWordWrap(true);
  m_simpleActivity = new QListWidget(simplePage);
  m_simpleActivity->setWordWrap(true);
  m_simpleActivity->setSpacing(4);
  m_simpleActivity->setToolTip(tr("A concise colour-coded summary of dry-run outcomes. No live action is performed by these entries."));
  auto* simpleRefresh = new QPushButton(tr("Refresh simple results"), simplePage);
  simpleLayout->addWidget(simpleIntro);
  simpleLayout->addWidget(simpleLegend);
  simpleLayout->addWidget(m_simpleActivity, 1);
  auto* simpleButtons = new QHBoxLayout(); simpleButtons->addStretch(); simpleButtons->addWidget(simpleRefresh);
  simpleLayout->addLayout(simpleButtons);
  const int simpleTab = tabs->addTab(simplePage, tr("Simple"));
  tabs->setTabToolTip(simpleTab, tr("See clear colour-coded outcomes from dry runs without the detailed diagnostic log."));

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
                                     m_cleanup, m_deleteData, m_confirmCleanup, m_retentionEnabled,
                                     m_retentionHours, m_retentionStrict, m_seedHours, m_ratio,
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
  connect(m_storageUnit, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &SettingsTorrentAutomation::changeStorageUnit);
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
  connect(simpleRefresh, &QPushButton::clicked, this, &SettingsTorrentAutomation::refreshSimpleActivity);
  connect(m_cleanup, &QCheckBox::toggled, this, &SettingsTorrentAutomation::updateCleanupControls);
  connect(m_retentionEnabled, &QCheckBox::toggled, this, &SettingsTorrentAutomation::updateCleanupControls);
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
  connect(setupWizard, &QPushButton::clicked, this, &SettingsTorrentAutomation::runSetupWizard);
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
  connect(m_retentionEnabled, &QCheckBox::clicked, this, [this](bool checked) {
    if (checked) QMessageBox::information(this, tr("Maximum retention enabled"),
      tr("Completed RSS Guard-managed torrents can now be removed when their time limit expires even if no storage space is needed. Run a dry test before using live mode."));
  });
  connect(m_retentionStrict, &QCheckBox::clicked, this, [this](bool checked) {
    if (checked && m_retentionEnabled->isChecked()) QMessageBox::warning(this, tr("Firm retention deadline"),
      tr("At the deadline, ratio, inactivity and upload-activity protections will no longer postpone removal. Protected tags, protected trackers and minimum-copy protection still apply."));
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
  m_storageUnit->blockSignals(true);
  m_storageUnit->setCurrentIndex(m_storageUnit->findData(m_config.storageDisplayUnit));
  m_currentStorageUnit = m_config.storageDisplayUnit;
  m_storageUnit->blockSignals(false);
  m_clients->horizontalHeaderItem(5)->setText(tr("Min free %1").arg(m_currentStorageUnit));
  m_clients->horizontalHeaderItem(7)->setText(tr("Capacity %1").arg(m_currentStorageUnit));
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
  m_retentionEnabled->setChecked(m_config.maximumRetentionEnabled);
  m_retentionHours->setValue(m_config.maximumRetentionHours);
  m_retentionStrict->setChecked(m_config.maximumRetentionStrict);
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
  m_config.storageDisplayUnit = m_storageUnit->currentData().toString();
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
  m_config.maximumRetentionEnabled = m_retentionEnabled->isChecked();
  m_config.maximumRetentionHours = m_retentionHours->value();
  m_config.maximumRetentionStrict = m_retentionStrict->isChecked();
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
  for (int row = 0; row < m_clients->rowCount(); ++row) {
    for (const int column : {5, 7, 8}) {
      bool valid = false;
      parseByteQuantity(m_clients->item(row, column)->text(),
                        column == 8 ? QStringLiteral("MiB") : m_config.storageDisplayUnit, &valid);
      if (valid) continue;
      QMessageBox::warning(this, tr("Invalid size or transfer rate"),
                           tr("%1 contains an invalid value in “%2”. Use a number with an optional unit, for example 2048 MiB, 750 GiB, 1.8 TiB, 500 MiB/s or 1.2 GiB/s.")
                             .arg(m_clients->item(row, 1)->text().section(QLatin1Char('\n'), 0, 0),
                                  m_clients->horizontalHeaderItem(column)->text()));
      onEndSaveSettings();
      return;
    }
  }
  m_config.clients.clear();
  for (int row = 0; row < m_clients->rowCount(); ++row) {
    TorrentAutomationClientPolicy policy;
    policy.clientId = m_clients->item(row, 1)->data(Qt::UserRole).toString();
    policy.enabled = m_clients->item(row, 0)->checkState() == Qt::Checked;
    policy.maxActiveDownloads = m_clients->item(row, 2)->text().toInt();
    policy.maxManagedTorrents = m_clients->item(row, 3)->text().toInt();
    policy.priority = qMax(1, m_clients->item(row, 4)->text().toInt());
    policy.minimumFreeBytes = parseByteQuantity(m_clients->item(row, 5)->text(), m_config.storageDisplayUnit);
    policy.targetFreePercent = qBound(0.0, m_clients->item(row, 6)->text().toDouble(), 100.0);
    policy.configuredCapacityBytes = parseByteQuantity(m_clients->item(row, 7)->text(), m_config.storageDisplayUnit);
    policy.maximumDownloadBytesPerSecond = parseByteQuantity(m_clients->item(row, 8)->text(), QStringLiteral("MiB"));
    policy.requestTimeoutSeconds = qMax(0, m_clients->item(row, 9)->text().toInt());
    policy.retryAttempts = qMax(-1, m_clients->item(row, 10)->text().toInt());
    policy.allowCleanup = m_clients->item(row, 11)->checkState() == Qt::Checked;
    m_config.clients.append(policy);
  }
  m_config.save(settings());
  onEndSaveSettings();
}

void SettingsTorrentAutomation::changeStorageUnit(int index) {
  if (index < 0 || m_clients == nullptr) return;
  const QString newUnit = m_storageUnit->itemData(index).toString();
  if (newUnit.isEmpty() || newUnit == m_currentStorageUnit) return;

  m_clients->blockSignals(true);
  for (int row = 0; row < m_clients->rowCount(); ++row) {
    for (const int column : {5, 7}) {
      QTableWidgetItem* item = m_clients->item(row, column);
      if (item == nullptr) continue;
      bool ok = false;
      const qint64 bytes = parseByteQuantity(item->text(), m_currentStorageUnit, &ok);
      if (ok) item->setText(formatByteQuantity(bytes, newUnit));
    }
  }
  m_clients->horizontalHeaderItem(5)->setText(tr("Min free %1").arg(newUnit));
  m_clients->horizontalHeaderItem(7)->setText(tr("Capacity %1").arg(newUnit));
  m_clients->blockSignals(false);
  m_currentStorageUnit = newUnit;
  dirtifySettings();
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
    m_clients->setItem(row, 5, new QTableWidgetItem(formatByteQuantity(policy.minimumFreeBytes, m_currentStorageUnit)));
    m_clients->setItem(row, 6, new QTableWidgetItem(QString::number(policy.targetFreePercent, 'f', 1)));
    m_clients->setItem(row, 7, new QTableWidgetItem(formatByteQuantity(policy.configuredCapacityBytes, m_currentStorageUnit)));
    m_clients->setItem(row, 8, new QTableWidgetItem(formatTransferRate(policy.maximumDownloadBytesPerSecond)));
    m_clients->setItem(row, 9, new QTableWidgetItem(QString::number(policy.requestTimeoutSeconds)));
    m_clients->setItem(row, 10, new QTableWidgetItem(QString::number(policy.retryAttempts)));
    m_clients->setItem(row, 11, cleanup);
    QString storageSource;
    if (client.capabilityFreeSpace) storageSource = policy.configuredCapacityBytes > 0
      ? tr("Live free + known total") : tr("Live free; total unknown");
    else if (m_config.reconciliationEnabled && client.capabilityTorrentList && policy.configuredCapacityBytes > 0)
      storageSource = tr("All-torrent estimate");
    else if (policy.configuredCapacityBytes > 0) storageSource = tr("Conservative estimate");
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
  refreshSimpleActivity();
}

void SettingsTorrentAutomation::refreshSimpleActivity() {
  if (!m_simpleActivity) return;
  m_simpleActivity->clear();
  const QJsonArray history = TorrentAutomationEngine::instance(qApp)->activityHistory();
  const int limit = qBound(50, m_config.historyLimit, 5000);
  int shown = 0;
  for (int index = history.size() - 1; index >= 0 && shown < limit; --index) {
    const QJsonObject event = history.at(index).toObject();
    const QString state = event.value(QStringLiteral("state")).toString();
    if (!state.startsWith(QStringLiteral("DRY RUN"))) continue;
    if (state == QStringLiteral("DRY RUN — ITEM") || state == QStringLiteral("DRY RUN — CLIENT") ||
        state == QStringLiteral("DRY RUN — CLEANUP TARGET")) continue;

    const QString title = event.value(QStringLiteral("title")).toString();
    const QString detail = event.value(QStringLiteral("detail")).toString();
    const QString clientId = event.value(QStringLiteral("clientId")).toString();
    QString clientName;
    for (const TorrentClientConfig& client : std::as_const(m_clientConfigs))
      if (client.id == clientId) { clientName = client.name; break; }

    QString action, summary;
    QColor foreground, background;
    if (state == QStringLiteral("DRY RUN — WOULD REMOVE") ||
        state == QStringLiteral("DRY RUN — WOULD REMOVE EXPIRED")) {
      action = tr("WOULD DELETE"); foreground = QColor(QStringLiteral("#6A1B9A")); background = QColor(QStringLiteral("#F3E5F5"));
      QRegularExpressionMatch match = QRegularExpression(
        QStringLiteral("Would remove [“\"](.+?)[”\"] from (.+?)(?: and delete its downloaded data| but keep its downloaded data)?; size ([^;]+); (.+?)\\. Estimated free space"),
        QRegularExpression::CaseInsensitiveOption).match(detail);
      summary = tr("%1%2").arg(match.hasMatch() ? match.captured(1) : (title.isEmpty() ? tr("Torrent") : title),
                                clientName.isEmpty() ? QString() : tr(" — %1").arg(clientName));
      if (match.hasMatch()) summary += tr(" — %1 • %2").arg(match.captured(3), match.captured(4));
      else summary += tr(" • %1").arg(detail.section(QStringLiteral(". No changes"), 0, 0));
    }
    else if (state == QStringLiteral("DRY RUN — PROTECTED") ||
             state == QStringLiteral("DRY RUN — RETENTION PROTECTED")) {
      action = tr("WOULD KEEP"); foreground = QColor(QStringLiteral("#1B5E20")); background = QColor(QStringLiteral("#E8F5E9"));
      summary = detail.section(QStringLiteral(". "), 0, 0);
    }
    else if (state == QStringLiteral("DRY RUN — CLEANUP INSUFFICIENT") ||
             state == QStringLiteral("DRY RUN — CLEANUP BLOCKED") ||
             (state == QStringLiteral("DRY RUN — FINAL") && detail.contains(tr("No client"), Qt::CaseInsensitive))) {
      action = tr("BLOCKED"); foreground = QColor(QStringLiteral("#B71C1C")); background = QColor(QStringLiteral("#FFEBEE"));
      summary = detail.section(QStringLiteral(". "), 0, 0);
    }
    else if (state == QStringLiteral("DRY RUN — HISTORICAL SAMPLE")) {
      action = tr("HISTORICAL SAMPLE"); foreground = QColor(QStringLiteral("#455A64")); background = QColor(QStringLiteral("#ECEFF1"));
      summary = detail.section(QStringLiteral(". "), 0, 0);
    }
    else if (state == QStringLiteral("DRY RUN — NO TORRENT") || state == QStringLiteral("DRY RUN — NO RULE") ||
             state == QStringLiteral("DRY RUN — DUPLICATE")) {
      action = tr("SKIPPED"); foreground = QColor(QStringLiteral("#455A64")); background = QColor(QStringLiteral("#ECEFF1"));
      summary = title.isEmpty() ? detail.section(QStringLiteral(". "), 0, 0)
                                : tr("%1 • %2").arg(title, detail.section(QStringLiteral(". "), 0, 0));
    }
    else if (state == QStringLiteral("DRY RUN — STATUS RETRY") || state == QStringLiteral("DRY RUN — SCHEDULE") ||
             state == QStringLiteral("DRY RUN — CLEANUP") || state == QStringLiteral("DRY RUN — WOULD MARK") ||
             state == QStringLiteral("DRY RUN — GRACE WAIT") || state == QStringLiteral("DRY RUN — RETRY") ||
             state == QStringLiteral("DRY RUN — RETENTION WAIT") || state == QStringLiteral("DRY RUN — RETENTION LIMIT")) {
      action = tr("WOULD WAIT / RETRY"); foreground = QColor(QStringLiteral("#8A5200")); background = QColor(QStringLiteral("#FFF3D6"));
      summary = detail.section(QStringLiteral(". "), 0, 0);
    }
    else if (state == QStringLiteral("DRY RUN — RETENTION")) {
      action = tr("SKIPPED"); foreground = QColor(QStringLiteral("#455A64")); background = QColor(QStringLiteral("#ECEFF1"));
      summary = detail.section(QStringLiteral(". "), 0, 0);
    }
    else if (state == QStringLiteral("DRY RUN — FINAL")) {
      // A successful final cleanup result is already represented by its individual delete rows.
      continue;
    }
    else if (state == QStringLiteral("DRY RUN")) {
      action = tr("WOULD SEND"); foreground = QColor(QStringLiteral("#0D47A1")); background = QColor(QStringLiteral("#E3F2FD"));
      summary = tr("%1%2").arg(title.isEmpty() ? tr("Torrent") : title,
                                clientName.isEmpty() ? QString() : tr(" → %1").arg(clientName));
      const QRegularExpressionMatch rule = QRegularExpression(QStringLiteral("Rule: ([^.]+)\\." )).match(detail);
      if (rule.hasMatch()) summary += tr(" • matched rule: %1").arg(rule.captured(1));
    }
    else continue;

    auto* item = new QListWidgetItem(QStringLiteral("●  %1     %2").arg(action, summary), m_simpleActivity);
    QFont itemFont = item->font(); itemFont.setBold(true); item->setFont(itemFont);
    item->setForeground(foreground); item->setBackground(background);
    item->setToolTip(tr("%1\n\nFull explanation: %2").arg(state, detail));
    item->setSizeHint(QSize(0, qMax(34, fontMetrics().lineSpacing() * 2)));
    ++shown;
  }
  if (m_simpleActivity->count() == 0) {
    auto* empty = new QListWidgetItem(tr("No dry-run outcomes yet. Use “Run dry test now”, then return here to review the results."), m_simpleActivity);
    empty->setForeground(QColor(QStringLiteral("#455A64")));
  }
}

void SettingsTorrentAutomation::applyQuickPreset(int preset) {
  if (preset <= PresetCustom || preset > PresetLongSeed) return;

  // Common reliable foundation. Presets deliberately do not overwrite RSS
  // rules, configured clients, per-client storage limits or protected names.
  m_enabled->setChecked(true);
  m_dryRun->setChecked(true);
  m_notifications->setChecked(true);
  m_strategy->setCurrentIndex(m_strategy->findData(static_cast<int>(TorrentRoutingStrategy::Balanced)));
  m_retryEnabled->setChecked(true);
  m_retryAttempts->setValue(3);
  m_retryInitialSeconds->setValue(60);
  m_retryMaximumSeconds->setValue(900);
  m_retryBackoff->setChecked(true);
  m_requestTimeoutSeconds->setValue(15);
  m_reconciliation->setChecked(true);
  m_reconciliationMinutes->setValue(30);
  m_reserveRemaining->setChecked(true);
  m_preventDuplicates->setChecked(true);
  m_circuitBreaker->setChecked(true);
  m_breakerFailures->setValue(3);
  m_breakerCooldown->setValue(15);
  m_breakerRecoverySuccesses->setValue(2);

  if (preset == PresetSafeTest) {
    m_cleanup->setChecked(false);
    m_deleteData->setChecked(false);
    m_confirmCleanup->setChecked(true);
    m_retentionEnabled->setChecked(false);
    updateCleanupControls();
    dirtifySettings();
    return;
  }

  m_cleanup->setChecked(true);
  m_deleteData->setChecked(true);
  m_confirmCleanup->setChecked(true);
  m_seedHoursEnabled->setChecked(true);
  m_ratioEnabled->setChecked(true);
  m_inactiveHoursEnabled->setChecked(true);
  m_maxRemovalsEnabled->setChecked(true);
  m_maxRemovals->setValue(1);
  m_cleanupStopGbEnabled->setChecked(true);
  m_cleanupStopGb->setValue(40.0);
  m_protectUploading->setChecked(true);
  m_protectUploadKib->setValue(256);
  m_protectUnknownSpeed->setChecked(true);
  m_protectRecentHours->setValue(24);
  m_cleanupGrace->setChecked(true);
  m_smartCleanup->setChecked(true);
  m_cleanupBatchPercent->setValue(5.0);

  if (preset == PresetBalanced) {
    m_seedHours->setValue(168);
    m_ratio->setValue(1.0);
    m_inactiveHours->setValue(24);
    m_cleanupGraceHours->setValue(24);
    m_retentionEnabled->setChecked(false);
    m_retentionHours->setValue(720);
    m_retentionStrict->setChecked(true);
  }
  else if (preset == PresetThirtyDay) {
    m_seedHours->setValue(168);
    m_ratio->setValue(1.0);
    m_inactiveHours->setValue(24);
    m_cleanupGraceHours->setValue(24);
    m_retentionEnabled->setChecked(true);
    m_retentionHours->setValue(720);
    m_retentionStrict->setChecked(true);
  }
  else if (preset == PresetQuickTurnaround) {
    m_seedHours->setValue(72);
    m_ratio->setValue(0.5);
    m_inactiveHours->setValue(6);
    m_cleanupStopGb->setValue(60.0);
    m_protectRecentHours->setValue(6);
    m_cleanupGraceHours->setValue(6);
    m_maxRemovals->setValue(3);
    m_retentionEnabled->setChecked(true);
    m_retentionHours->setValue(168);
    m_retentionStrict->setChecked(true);
  }
  else if (preset == PresetLongSeed) {
    m_seedHours->setValue(720);
    m_ratio->setValue(2.0);
    m_inactiveHours->setValue(72);
    m_cleanupGraceHours->setValue(48);
    m_retentionEnabled->setChecked(true);
    m_retentionHours->setValue(2160);
    m_retentionStrict->setChecked(false);
  }

  updateCleanupControls();
  dirtifySettings();
}

void SettingsTorrentAutomation::runSetupWizard() {
  const QList<TorrentAutomationRule> originalRules = m_config.rules;
  QWizard wizard(this);
  wizard.setWindowTitle(tr("Torrent automation setup wizard"));
  wizard.setWizardStyle(QWizard::ModernStyle);
  wizard.setOption(QWizard::NoBackButtonOnStartPage);
  wizard.setOption(QWizard::HaveHelpButton);
  wizard.setButtonText(QWizard::NextButton, tr("Next step  ›"));
  wizard.setButtonText(QWizard::BackButton, tr("‹  Previous step"));
  wizard.setButtonText(QWizard::FinishButton, tr("Apply wizard choices"));
  wizard.setButtonText(QWizard::CancelButton, tr("Cancel without changes"));
  wizard.setButtonText(QWizard::HelpButton, tr("Explain this page"));
  wizard.setMinimumSize(980, 720);
  const QPalette wizardPalette = wizard.palette();
  const bool darkWizard = wizardPalette.color(QPalette::Window).lightness() < 128 ||
                          wizardPalette.color(QPalette::Base).lightness() < 128;
  const QString pageBackground = wizardPalette.color(QPalette::Window).name();
  const QString pageText = wizardPalette.color(QPalette::WindowText).name();
  const QString fieldBackground = wizardPalette.color(QPalette::Base).name();
  const QString border = darkWizard ? QStringLiteral("#71829A") : QStringLiteral("#AAB6C5");
  const QString noteBackground = darkWizard ? QStringLiteral("#26364F") : QStringLiteral("#EAF2FF");
  const QString noteText = darkWizard ? QStringLiteral("#F5F8FC") : QStringLiteral("#17243D");
  const QString noteBorder = darkWizard ? QStringLiteral("#6F8FB8") : QStringLiteral("#9DB4D3");
  const QString warningBackground = darkWizard ? QStringLiteral("#4A3B12") : QStringLiteral("#FFF1C7");
  const QString warningText = darkWizard ? QStringLiteral("#FFF0A6") : QStringLiteral("#543B00");
  const QString warningBorder = darkWizard ? QStringLiteral("#C59D35") : QStringLiteral("#D3A72E");
  wizard.setStyleSheet(QStringLiteral(
    "QWizard, QWizardPage { background: %1; color: %2; }"
    "QWizardPage QLabel, QWizardPage QCheckBox, QWizardPage QRadioButton { color: %2; }"
    "QWizardPage QLabel#wizardNote { background: %3; color: %4; border: 1px solid %5; "
    "  border-radius: 8px; padding: 12px; margin-top: 6px; }"
    "QWizardPage QLabel#wizardWarning { background: %6; color: %7; border: 1px solid %8; "
    "  border-radius: 8px; padding: 12px; font-weight: 600; }"
    "QWizardPage QGroupBox { color: %2; font-weight: 600; border: 1px solid %9; border-radius: 8px; "
    "  margin-top: 12px; padding: 12px 8px 8px 8px; }"
    "QWizardPage QGroupBox::title { background: %1; subcontrol-origin: margin; left: 12px; padding: 0 5px; }"
    "QWizardPage QLineEdit, QWizardPage QComboBox, QWizardPage QSpinBox, QWizardPage QDoubleSpinBox, "
    "QWizardPage QTableWidget { background: %10; color: %2; border: 1px solid %9; }"
    "QWizard QPushButton { min-height: 28px; padding-left: 12px; padding-right: 12px; }"
    "QWizard QCheckBox { spacing: 8px; min-height: 24px; }"
    "QWizard QLineEdit, QWizard QSpinBox, QWizard QDoubleSpinBox, QWizard QComboBox { min-height: 26px; }"
  ).arg(pageBackground).arg(pageText).arg(noteBackground).arg(noteText).arg(noteBorder)
    .arg(warningBackground).arg(warningText).arg(warningBorder).arg(border).arg(fieldBackground));

  QHash<int, QString> detailedHelp;

  const auto page = [&wizard](const QString& title, const QString& text) {
    auto* result = new QWizardPage(&wizard);
    result->setTitle(title);
    result->setSubTitle(text);
    result->setLayout(new QVBoxLayout(result));
    wizard.addPage(result);
    return result;
  };
  const auto note = [](QWizardPage* owner, const QString& text) {
    auto* label = new QLabel(text, owner);
    label->setObjectName(QStringLiteral("wizardNote"));
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);
    owner->layout()->addWidget(label);
    return label;
  };
  const auto form = [](QWizardPage* owner) {
    auto* result = new QFormLayout();
    static_cast<QVBoxLayout*>(owner->layout())->addLayout(result);
    return result;
  };
  const auto warning = [](QWizardPage* owner, const QString& text) {
    auto* label = new QLabel(text, owner);
    label->setObjectName(QStringLiteral("wizardWarning"));
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);
    owner->layout()->addWidget(label);
    return label;
  };
  const auto check = [](QWidget* owner, const QString& text, bool value, const QString& tip) {
    auto* result = new QCheckBox(text, owner);
    result->setChecked(value); result->setToolTip(tip);
    return result;
  };
  const auto integer = [](QWidget* owner, int value, int minimum, int maximum, const QString& suffix,
                          const QString& tip) {
    auto* result = new QSpinBox(owner);
    result->setRange(minimum, maximum); result->setValue(value); result->setSuffix(suffix); result->setToolTip(tip);
    return result;
  };
  const auto decimal = [](QWidget* owner, double value, double minimum, double maximum, int decimals,
                          const QString& suffix, const QString& tip) {
    auto* result = new QDoubleSpinBox(owner);
    result->setRange(minimum, maximum); result->setDecimals(decimals); result->setValue(value);
    result->setSuffix(suffix); result->setToolTip(tip);
    return result;
  };
  const auto hours = [](QWidget* owner, int value, bool allow24) {
    auto* result = new QComboBox(owner);
    for (int hour = 0; hour <= (allow24 ? 24 : 23); ++hour)
      result->addItem(QStringLiteral("%1:00").arg(hour, 2, 10, QLatin1Char('0')), hour);
    result->setCurrentIndex(result->findData(value));
    return result;
  };

  QWizardPage* welcome = page(tr("Welcome"), tr("This wizard explains and configures the complete torrent-automation feature one section at a time."));
  auto* welcomeHeading = new QLabel(tr("Let’s set this up together"), welcome);
  QFont welcomeFont = welcomeHeading->font(); welcomeFont.setPointSize(welcomeFont.pointSize() + 5); welcomeFont.setBold(true);
  welcomeHeading->setFont(welcomeFont); welcome->layout()->addWidget(welcomeHeading);
  note(welcome, tr("<b>Safe starting point:</b> the wizard keeps <b>Dry run</b> enabled unless you deliberately turn it off. "
                   "Dry run evaluates real rules and client status but does not send or delete anything.<br><br>"
                   "Use <b>Next step</b> to move forward and <b>Previous step</b> whenever you want to change an answer. "
                   "Select <b>Explain this page</b> for a fuller description. Nothing is applied until the final button is selected."));
  note(welcome, tr("<b>What you will decide:</b><br>1. Whether to run safely in test mode<br>2. Which client receives each torrent<br>"
                   "3. Which RSS items are allowed<br>4. What happens when a client is unavailable<br>5. Whether managed torrents are removed for low space, at a fixed age, or both"));
  detailedHelp.insert(wizard.pageIds().constLast(), tr("This wizard changes only Torrent automation settings. It does not alter your feeds, existing torrents or configured torrent-client passwords. Cancelling restores the settings that existed before the wizard opened."));
  welcome->layout()->addItem(new QSpacerItem(1, 1, QSizePolicy::Minimum, QSizePolicy::Expanding));

  QWizardPage* presetPage = page(tr("Quick Set (optional)"),
    tr("Choose a common starting configuration or skip this page and answer every section yourself."));
  auto* wizardPresetBox = new QGroupBox(tr("Common configurations"), presetPage);
  auto* wizardPresetLayout = new QVBoxLayout(wizardPresetBox);
  auto* wizardPresetRow = new QHBoxLayout();
  auto* wizardPresetChoice = new QComboBox(wizardPresetBox);
  for (int preset = PresetCustom; preset <= PresetLongSeed; ++preset)
    wizardPresetChoice->addItem(quickPresetName(preset), preset);
  auto* wizardPresetApply = new QPushButton(tr("Review and use preset"), wizardPresetBox);
  auto* wizardPresetDetail = new QLabel(quickPresetDetail(PresetCustom), wizardPresetBox);
  wizardPresetDetail->setWordWrap(true); wizardPresetDetail->setTextFormat(Qt::RichText);
  wizardPresetApply->setEnabled(false);
  wizardPresetRow->addWidget(wizardPresetChoice, 1); wizardPresetRow->addWidget(wizardPresetApply);
  wizardPresetLayout->addLayout(wizardPresetRow); wizardPresetLayout->addWidget(wizardPresetDetail);
  presetPage->layout()->addWidget(wizardPresetBox);
  connect(wizardPresetChoice, QOverload<int>::of(&QComboBox::currentIndexChanged), &wizard,
          [wizardPresetChoice, wizardPresetDetail, wizardPresetApply]() {
    const int preset = wizardPresetChoice->currentData().toInt();
    wizardPresetDetail->setText(quickPresetDetail(preset));
    wizardPresetApply->setEnabled(preset != PresetCustom);
  });
  note(presetPage, tr("A preset fills the later wizard pages for you. You can still move through every page and change any answer before Finish. It never changes clients, capacities, cleanup permission per client, protected names or RSS rules."));
  detailedHelp.insert(wizard.pageIds().constLast(), tr("Quick Set is optional. Each preset applies a common combination of routing, retry, storage maintenance and cleanup values to this wizard only. Every preset keeps Dry run enabled. The preset does not save anything, and cancelling the wizard discards it."));
  presetPage->layout()->addItem(new QSpacerItem(1, 1, QSizePolicy::Minimum, QSizePolicy::Expanding));

  QWizardPage* basics = page(tr("1. Safety and basic behaviour"),
    tr("Choose whether automation runs, whether it remains test-only, and how its decisions are reported."));
  auto* basicsForm = form(basics);
  auto* enabled = check(basics, tr("Enable torrent automation"), m_enabled->isChecked(),
                        tr("The master switch. When off, RSS items are never routed automatically."));
  auto* dryRun = check(basics, tr("Dry run — test decisions without sending or deleting"), m_dryRun->isChecked(),
                       tr("Recommended until the Simple and Activity results match what you expect."));
  auto* notifications = check(basics, tr("Show automation notifications"), m_notifications->isChecked(),
                              tr("Show clear send, wait, block, retry and cleanup outcomes."));
  auto* history = integer(basics, m_historyLimit->value(), 50, 5000, QString(),
                          tr("Maximum decision records retained. Example: 500 is normally ample."));
  basicsForm->addRow(enabled); basicsForm->addRow(dryRun); basicsForm->addRow(notifications);
  basicsForm->addRow(tr("Activity history entries:"), history);
  auto* dryRunStatus = warning(basics, QString());
  const auto updateDryRunStatus = [dryRun, dryRunStatus]() {
    dryRunStatus->setText(dryRun->isChecked()
      ? QObject::tr("SAFE TEST MODE IS ON — RSS Guard will explain what it would do, but it will not send or delete anything.")
      : QObject::tr("LIVE MODE SELECTED — matching items may be sent after you save. Only turn this off after reviewing successful dry-run results."));
  };
  connect(dryRun, &QCheckBox::toggled, &wizard, updateDryRunStatus); updateDryRunStatus();
  note(basics, tr("<b>Simple recommendation:</b> Enable automation, keep Dry run on, keep notifications on and leave the history at 500. "
                  "After setup, run a dry test and review the Simple results before considering live mode."));
  detailedHelp.insert(wizard.pageIds().constLast(), tr("Enable torrent automation is the master switch. Dry run keeps the complete decision process active but replaces sends and deletions with reports. Notifications show important outcomes on screen. Activity history controls only how many audit entries are remembered; it does not limit feed articles or torrents."));
  basics->layout()->addItem(new QSpacerItem(1, 1, QSizePolicy::Minimum, QSizePolicy::Expanding));

  QWizardPage* routing = page(tr("2. Routing and size estimates"),
    tr("Decide how RSS Guard selects between eligible clients and how it reserves space when a feed does not provide a torrent size."));
  auto* routingForm = form(routing);
  auto* strategy = new QComboBox(routing);
  for (int index = 0; index < m_strategy->count(); ++index)
    strategy->addItem(m_strategy->itemText(index), m_strategy->itemData(index));
  strategy->setCurrentIndex(strategy->findData(m_strategy->currentData()));
  auto* unknownSize = decimal(routing, m_unknownSizeGb->value(), 0.1, 1000000.0, 1, tr(" GiB"),
                              tr("Example: reserve 10 GiB when the real size is unknown. Exact magnet xl sizes still take precedence."));
  auto* storageUnit = new QComboBox(routing);
  for (int index = 0; index < m_storageUnit->count(); ++index)
    storageUnit->addItem(m_storageUnit->itemText(index), m_storageUnit->itemData(index));
  storageUnit->setCurrentIndex(storageUnit->findData(m_storageUnit->currentData()));
  routingForm->addRow(tr("Routing strategy:"), strategy);
  routingForm->addRow(tr("Assumed unknown torrent size:"), unknownSize);
  routingForm->addRow(tr("Storage input/display unit:"), storageUnit);
  note(routing, tr("<b>Balanced (recommended)</b> considers free-space ratio, active and queued downloads, download rate and priority. "
                   "Priority order always favours the lowest priority number; Round robin rotates evenly."));
  auto* strategyExplanation = note(routing, QString());
  const QStringList wizardStrategyHelp{
    tr("<b>Priority order:</b> always try priority 1 first, then 2, then 3. Best when one client should normally receive everything."),
    tr("<b>Least busy:</b> choose the client with the fewest active downloads. Best when the clients have similar storage."),
    tr("<b>Most free space:</b> choose the client reporting the largest free-space amount. Best when storage capacity is the main concern."),
    tr("<b>Round robin:</b> take turns between clients. This is simple and predictable but does not favour the healthiest client."),
    tr("<b>Priority-biased:</b> distribute work while still favouring lower priority numbers."),
    tr("<b>Balanced (recommended):</b> combine free space, workload, speed and priority to make the safest overall choice.")};
  const auto updateStrategyHelp = [strategy, strategyExplanation, wizardStrategyHelp]() {
    const int index = qBound(0, strategy->currentIndex(), int(wizardStrategyHelp.size()) - 1);
    strategyExplanation->setText(wizardStrategyHelp.at(index));
  };
  connect(strategy, QOverload<int>::of(&QComboBox::currentIndexChanged), &wizard, updateStrategyHelp); updateStrategyHelp();
  detailedHelp.insert(wizard.pageIds().constLast(), tr("Routing happens only after a rule matches and all client safety limits are checked. The strategy never overrides a full, unavailable or disabled client. The unknown-size reservation is a temporary safety estimate used only when the RSS item and magnet link provide no exact size. GiB is the clearest normal choice for storage values."));
  routing->layout()->addItem(new QSpacerItem(1, 1, QSizePolicy::Minimum, QSizePolicy::Expanding));

  QWizardPage* clients = page(tr("3. Clients and limits"),
    tr("Choose which configured clients participate and set their safety limits. Zero disables a numeric limit; priority 1 is highest."));
  auto* clientTable = new QTableWidget(clients);
  clientTable->setColumnCount(m_clients->columnCount());
  clientTable->setRowCount(m_clients->rowCount());
  for (int column = 0; column < m_clients->columnCount(); ++column)
    clientTable->setHorizontalHeaderItem(column, new QTableWidgetItem(*m_clients->horizontalHeaderItem(column)));
  for (int row = 0; row < m_clients->rowCount(); ++row) {
    for (int column = 0; column < m_clients->columnCount(); ++column) {
      const QTableWidgetItem* source = m_clients->item(row, column);
      if (source != nullptr) clientTable->setItem(row, column, new QTableWidgetItem(*source));
    }
  }
  clientTable->verticalHeader()->setVisible(false);
  clientTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  clientTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  static_cast<QVBoxLayout*>(clients->layout())->addWidget(clientTable, 1);
  auto* clientColumnHelp = note(clients, tr("Select a cell to see a plain-language explanation of that option."));
  connect(clientTable, &QTableWidget::currentCellChanged, &wizard,
          [clientTable, clientColumnHelp](int, int column) {
    if (column < 0 || clientTable->horizontalHeaderItem(column) == nullptr) return;
    const QTableWidgetItem* header = clientTable->horizontalHeaderItem(column);
    clientColumnHelp->setText(QStringLiteral("<b>%1</b><br>%2")
      .arg(header->text().toHtmlEscaped(), header->toolTip().toHtmlEscaped()));
  });
  note(clients, tr("<b>Examples:</b> Max active 3 blocks a fourth active download. Min free 20 GiB preserves a fixed reserve. "
                   "Target free 10% scales with disk size. Capacity is a fallback torrent-storage budget when live space is unavailable; subtract space reserved for unrelated files before entering it. "
                   "Cleanup must remain off unless that client's list/removal capability has been tested."));
  detailedHelp.insert(wizard.pageIds().constLast(), tr("Use decides whether a client participates. Max active prevents adding work when too many downloads are running. Max managed limits only torrents previously sent by RSS Guard. Priority 1 is preferred over 2. Minimum free keeps a fixed reserve, while Target free keeps a percentage reserve. Capacity is a fallback torrent-storage budget for APIs without live disk-space reporting: RSS Guard subtracts every torrent reported by the client, including manually added torrents, and reserves pending managed downloads. To avoid overestimating space, subtract unrelated files from the value you enter. Max down rate avoids heavily downloading clients. Timeout and Retries override the global values. Cleanup gives permission to consider that client for the later cleanup rules; it does not immediately delete anything."));

  QWizardPage* rules = page(tr("4. RSS matching rules"),
    tr("Rules decide which feed items automation may process and which clients they may use."));
  auto* ruleList = new QListWidget(rules);
  const auto refreshWizardRules = [this, ruleList]() {
    ruleList->clear();
    for (const TorrentAutomationRule& rule : std::as_const(m_config.rules)) {
      auto* item = new QListWidgetItem(rule.name.isEmpty() ? tr("Unnamed rule") : rule.name, ruleList);
      item->setCheckState(rule.enabled ? Qt::Checked : Qt::Unchecked);
      item->setToolTip(tr("Required: %1\nExcluded: %2\nTitle expression: %3\nAllowed clients: %4")
        .arg(rule.requiredText.isEmpty() ? tr("any") : rule.requiredText,
             rule.excludedText.isEmpty() ? tr("none") : rule.excludedText,
             rule.titleRegularExpression.isEmpty() ? tr("none") : rule.titleRegularExpression,
             rule.clientIds.isEmpty() ? tr("any enabled client") : rule.clientIds.join(QStringLiteral(", "))));
    }
  };
  refreshWizardRules();
  static_cast<QVBoxLayout*>(rules->layout())->addWidget(ruleList, 1);
  auto* ruleButtons = new QHBoxLayout();
  auto* addRuleButton = new QPushButton(tr("Add rule…"), rules);
  auto* editRuleButton = new QPushButton(tr("Edit selected…"), rules);
  auto* removeRuleButton = new QPushButton(tr("Remove selected"), rules);
  ruleButtons->addWidget(addRuleButton); ruleButtons->addWidget(editRuleButton); ruleButtons->addWidget(removeRuleButton); ruleButtons->addStretch();
  static_cast<QVBoxLayout*>(rules->layout())->addLayout(ruleButtons);
  note(rules, tr("<b>Example:</b> Required text <i>1080p</i>, excluded text <i>CAM</i>, and an allowed-client selection. "
                 "Leave a field empty when you do not want that restriction. Rules are checked from top to bottom."));
  warning(rules, tr("At least one enabled rule is required before automatic RSS processing can match anything."));
  detailedHelp.insert(wizard.pageIds().constLast(), tr("A rule is a filter. Feed selection limits where it applies. Required text must be present; excluded text must not be present. A title expression is an advanced regular-expression filter and can be left empty. Minimum and maximum size reject torrents outside the chosen range. Allowed clients restrict the destinations for that rule. The first enabled matching rule wins, so put more specific rules before broad rules."));
  connect(addRuleButton, &QPushButton::clicked, &wizard, [this, refreshWizardRules]() { addRule(); refreshWizardRules(); });
  connect(editRuleButton, &QPushButton::clicked, &wizard, [this, ruleList, refreshWizardRules]() {
    if (ruleList->currentRow() < 0) return;
    m_rules->setCurrentRow(ruleList->currentRow()); editRule(); refreshWizardRules();
  });
  connect(removeRuleButton, &QPushButton::clicked, &wizard, [this, ruleList, refreshWizardRules]() {
    if (ruleList->currentRow() < 0) return;
    m_rules->setCurrentRow(ruleList->currentRow()); removeRule(); refreshWizardRules();
  });
  connect(ruleList, &QListWidget::itemChanged, &wizard, [this, ruleList](QListWidgetItem* item) {
    const int row = ruleList->row(item);
    if (row >= 0 && row < m_config.rules.size()) m_config.rules[row].enabled = item->checkState() == Qt::Checked;
  });

  QWizardPage* retries = page(tr("5. Retries and request health"),
    tr("Temporary faults can be retried without repeatedly hammering an unavailable seedbox."));
  auto* retryForm = form(retries);
  auto* retryEnabled = check(retries, tr("Retry temporary connection and capacity failures"), m_retryEnabled->isChecked(), tr("Authentication and permanent configuration failures are not retried."));
  auto* retryAttempts = integer(retries, m_retryAttempts->value(), 0, 20, QString(), tr("Retries after the first attempt. Example: 3 means up to four total attempts."));
  auto* retryInitial = integer(retries, m_retryInitialSeconds->value(), 1, 86400, tr(" seconds"), tr("Delay before the first retry."));
  auto* retryMaximum = integer(retries, m_retryMaximumSeconds->value(), 1, 86400, tr(" seconds"), tr("Upper limit for later retry delays."));
  auto* retryBackoff = check(retries, tr("Increase the delay after each failure"), m_retryBackoff->isChecked(), tr("Exponential backoff gives an unhealthy server time to recover."));
  auto* requestTimeout = integer(retries, m_requestTimeoutSeconds->value(), 5, 300, tr(" seconds"), tr("Default client request timeout. Example: 15 seconds."));
  retryForm->addRow(retryEnabled); retryForm->addRow(tr("Retries after first attempt:"), retryAttempts);
  retryForm->addRow(tr("Initial delay:"), retryInitial); retryForm->addRow(tr("Maximum delay:"), retryMaximum);
  retryForm->addRow(retryBackoff); retryForm->addRow(tr("Default request timeout:"), requestTimeout);
  const auto updateRetryEditors = [retryEnabled, retryAttempts, retryInitial, retryMaximum, retryBackoff]() {
    const bool on = retryEnabled->isChecked(); retryAttempts->setEnabled(on); retryInitial->setEnabled(on);
    retryMaximum->setEnabled(on); retryBackoff->setEnabled(on);
  };
  connect(retryEnabled, &QCheckBox::toggled, &wizard, updateRetryEditors); updateRetryEditors();
  note(retries, tr("<b>Recommended starting values:</b> 3 retries, 60-second first delay, 900-second maximum, increasing delay on and a 15-second request timeout."));
  detailedHelp.insert(wizard.pageIds().constLast(), tr("A timeout means RSS Guard stopped waiting for one API request; it does not prove the torrent client is permanently offline. Retry attempts are additional tries after the original one. Increasing delay uses progressively longer waits, which protects an overloaded server. The maximum delay caps that growth. Authentication failures and invalid settings are not treated as temporary and are not endlessly retried."));
  retries->layout()->addItem(new QSpacerItem(1, 1, QSizePolicy::Minimum, QSizePolicy::Expanding));

  QWizardPage* maintenance = page(tr("6. Storage, duplicates and scheduling"),
    tr("Keep RSS Guard's estimates aligned with live clients, protect unfinished downloads and isolate repeatedly failing clients."));
  auto* maintenanceForm = form(maintenance);
  auto* reconciliation = check(maintenance, tr("Reconcile managed storage with live torrent lists"), m_reconciliation->isChecked(), tr("Recommended when torrents can also be changed outside RSS Guard."));
  auto* reserve = check(maintenance, tr("Reserve unfinished bytes"), m_reserveRemaining->isChecked(), tr("Prevents several simultaneous downloads from overcommitting disk space."));
  auto* duplicates = check(maintenance, tr("Skip torrents already present on another client"), m_preventDuplicates->isChecked(), tr("Uses the magnet info hash when available."));
  auto* reconciliationMinutes = integer(maintenance, m_reconciliationMinutes->value(), 1, 1440, tr(" minutes"), tr("Example: 30 minutes."));
  auto* breaker = check(maintenance, tr("Temporarily sideline repeatedly failing clients"), m_circuitBreaker->isChecked(), tr("Other healthy clients continue to be considered."));
  auto* breakerFailures = integer(maintenance, m_breakerFailures->value(), 1, 20, QString(), tr("Consecutive failures before cooldown."));
  auto* breakerCooldown = integer(maintenance, m_breakerCooldown->value(), 1, 1440, tr(" minutes"), tr("How long to wait before recovery checks."));
  auto* breakerRecovery = integer(maintenance, m_breakerRecoverySuccesses->value(), 1, 10, QString(), tr("Successful checks required before reuse."));
  auto* schedule = check(maintenance, tr("Limit unattended routing to set hours"), m_schedule->isChecked(), tr("Outside the window, jobs wait in the persistent queue."));
  auto* scheduleStart = hours(maintenance, m_scheduleStart->currentData().toInt(), false);
  auto* scheduleEnd = hours(maintenance, m_scheduleEnd->currentData().toInt(), true);
  auto* scheduleWidget = new QWidget(maintenance); auto* scheduleLayout = new QHBoxLayout(scheduleWidget);
  scheduleLayout->setContentsMargins(0, 0, 0, 0); scheduleLayout->addWidget(scheduleStart); scheduleLayout->addWidget(new QLabel(tr("to"), scheduleWidget)); scheduleLayout->addWidget(scheduleEnd);
  maintenanceForm->addRow(reconciliation); maintenanceForm->addRow(reserve); maintenanceForm->addRow(duplicates);
  maintenanceForm->addRow(tr("Reconciliation interval:"), reconciliationMinutes); maintenanceForm->addRow(breaker);
  maintenanceForm->addRow(tr("Failures before cooldown:"), breakerFailures); maintenanceForm->addRow(tr("Cooldown:"), breakerCooldown);
  maintenanceForm->addRow(tr("Recovery checks:"), breakerRecovery); maintenanceForm->addRow(schedule); maintenanceForm->addRow(tr("Routing window:"), scheduleWidget);
  const auto updateMaintenanceWizard = [reconciliation, reserve, reconciliationMinutes, breaker, breakerFailures,
                                         breakerCooldown, breakerRecovery, schedule, scheduleStart, scheduleEnd]() {
    reconciliationMinutes->setEnabled(reconciliation->isChecked()); reserve->setEnabled(reconciliation->isChecked());
    breakerFailures->setEnabled(breaker->isChecked()); breakerCooldown->setEnabled(breaker->isChecked());
    breakerRecovery->setEnabled(breaker->isChecked()); scheduleStart->setEnabled(schedule->isChecked());
    scheduleEnd->setEnabled(schedule->isChecked());
  };
  connect(reconciliation, &QCheckBox::toggled, &wizard, updateMaintenanceWizard);
  connect(breaker, &QCheckBox::toggled, &wizard, updateMaintenanceWizard);
  connect(schedule, &QCheckBox::toggled, &wizard, updateMaintenanceWizard); updateMaintenanceWizard();
  note(maintenance, tr("<b>Plain English:</b> reconciliation keeps RSS Guard's figures honest; duplicate protection avoids sending the same magnet twice; "
                       "the circuit breaker temporarily ignores a repeatedly failing server; the routing window controls what hours unattended sends may begin."));
  detailedHelp.insert(wizard.pageIds().constLast(), tr("Reconciliation compares RSS Guard's managed ledger with the real torrent list. Reserving unfinished bytes accounts for data that has not downloaded yet. Duplicate protection compares magnet hashes across reachable clients. The circuit breaker pauses checks after repeated failures, then requires successful recovery checks. Outside a routing window, work is kept safely in the persistent queue rather than discarded."));

  QWizardPage* cleanup = page(tr("7. Safe cleanup"),
    tr("Cleanup is optional and potentially destructive. Every enabled safeguard must pass before an RSS Guard-managed torrent can be removed."));
  auto* cleanupForm = form(cleanup);
  auto* cleanupEnabled = check(cleanup, tr("Enable automatic safe cleanup"), m_cleanup->isChecked(), tr("Leave off until dry-run cleanup outcomes have been reviewed."));
  auto* deleteData = check(cleanup, tr("Also delete downloaded data"), m_deleteData->isChecked(), tr("Permanent and cannot be undone. Off removes only the torrent job."));
  auto* confirmCleanup = check(cleanup, tr("Ask before every removal"), m_confirmCleanup->isChecked(), tr("Strongly recommended during setup."));
  auto* seedEnabled = check(cleanup, tr("Require minimum completed/seeding age"), m_seedHoursEnabled->isChecked(), tr("Example: 168 hours is seven days."));
  auto* seedHours = integer(cleanup, m_seedHours->value(), 0, 100000, tr(" hours"), tr("Minimum completed/seeding age."));
  auto* ratioEnabled = check(cleanup, tr("Require minimum share ratio"), m_ratioEnabled->isChecked(), tr("Only torrents meeting the ratio can be considered."));
  auto* ratio = decimal(cleanup, m_ratio->value(), 0.0, 1000.0, 2, QString(), tr("Example: 1.0 means uploaded as much as downloaded."));
  auto* inactiveEnabled = check(cleanup, tr("Require minimum inactivity"), m_inactiveHoursEnabled->isChecked(), tr("Protect recently active torrents."));
  auto* inactiveHours = integer(cleanup, m_inactiveHours->value(), 0, 100000, tr(" hours"), tr("Minimum time with no recent transfer activity."));
  auto* maxEnabled = check(cleanup, tr("Limit removals per run"), m_maxRemovalsEnabled->isChecked(), tr("A hard safety cap; 1 is the safest starting value."));
  auto* maxRemovals = integer(cleanup, m_maxRemovals->value(), 1, 25, QString(), tr("Maximum removals in one processing run."));
  auto* stopEnabled = check(cleanup, tr("Stop at a target free-space level"), m_cleanupStopGbEnabled->isChecked(), tr("Cleanup stops after reaching this free-space target."));
  auto* stopSpace = decimal(cleanup, m_cleanupStopGb->value(), 0.0, 1000000.0, 1, tr(" GiB"), tr("Example: 40 GiB."));
  cleanupForm->addRow(cleanupEnabled); cleanupForm->addRow(deleteData); cleanupForm->addRow(confirmCleanup);
  cleanupForm->addRow(seedEnabled, seedHours); cleanupForm->addRow(ratioEnabled, ratio);
  cleanupForm->addRow(inactiveEnabled, inactiveHours); cleanupForm->addRow(maxEnabled, maxRemovals);
  cleanupForm->addRow(stopEnabled, stopSpace);
  const auto updateCleanupEditors = [seedEnabled, seedHours, ratioEnabled, ratio, inactiveEnabled, inactiveHours,
                                      maxEnabled, maxRemovals, stopEnabled, stopSpace, cleanupEnabled, deleteData,
                                      confirmCleanup]() {
    seedHours->setEnabled(seedEnabled->isChecked()); ratio->setEnabled(ratioEnabled->isChecked());
    inactiveHours->setEnabled(inactiveEnabled->isChecked()); maxRemovals->setEnabled(maxEnabled->isChecked());
    stopSpace->setEnabled(stopEnabled->isChecked());
    if (!cleanupEnabled->isChecked()) { deleteData->setChecked(false); confirmCleanup->setChecked(true); }
    deleteData->setEnabled(cleanupEnabled->isChecked()); confirmCleanup->setEnabled(cleanupEnabled->isChecked());
  };
  for (QCheckBox* option : {cleanupEnabled, seedEnabled, ratioEnabled, inactiveEnabled, maxEnabled, stopEnabled})
    connect(option, &QCheckBox::toggled, &wizard, updateCleanupEditors);
  updateCleanupEditors();
  auto* cleanupWarning = warning(cleanup, QString());
  const auto updateCleanupWarning = [cleanupEnabled, deleteData, cleanupWarning]() {
    cleanupWarning->setText(!cleanupEnabled->isChecked()
      ? QObject::tr("CLEANUP IS OFF — no torrent or downloaded data will be removed automatically.")
      : (deleteData->isChecked()
          ? QObject::tr("HIGH-RISK CHOICE — eligible torrent jobs and their downloaded files may be permanently deleted.")
          : QObject::tr("CLEANUP ENABLED — eligible torrent jobs may be removed, but downloaded files will be kept.")));
  };
  connect(cleanupEnabled, &QCheckBox::toggled, &wizard, updateCleanupWarning);
  connect(deleteData, &QCheckBox::toggled, &wizard, updateCleanupWarning); updateCleanupWarning();
  note(cleanup, tr("<b>This page controls storage-pressure cleanup.</b> It acts only when a client needs room. The next page separately controls removal at a fixed age, even when storage is not low."));
  detailedHelp.insert(wizard.pageIds().constLast(), tr("The cleanup master switch permits both cleanup types. Storage-pressure cleanup runs only when an opted-in client needs room for a new torrent and can recover space only when downloaded-data deletion is enabled. Minimum age, ratio and inactivity are separate gates; every enabled gate must pass. Maximum removals is a per-run emergency limit. The target free-space value tells cleanup when it has recovered enough room. Deleting data removes the actual files and cannot be undone."));

  QWizardPage* retention = page(tr("8. Maximum retention time"),
    tr("Choose whether a completed RSS Guard-managed torrent must be removed after a fixed time, regardless of whether storage space is needed."));
  auto* retentionForm = form(retention);
  auto* retentionEnabled = check(retention, tr("Remove completed torrents after a maximum time"), m_retentionEnabled->isChecked(),
                                 tr("This runs periodically even when storage space is not low."));
  auto* retentionHours = integer(retention, m_retentionHours->value(), 1, 100000, tr(" hours"),
                                 tr("Examples: 24 = 1 day, 168 = 7 days, 720 = 30 days."));
  auto* retentionStrict = check(retention, tr("Make the maximum time a firm deadline"), m_retentionStrict->isChecked(),
                                tr("Ignore ratio, inactivity and upload-activity delays after expiry. Absolute protections still apply."));
  retentionForm->addRow(retentionEnabled); retentionForm->addRow(tr("Remove after:"), retentionHours);
  retentionForm->addRow(retentionStrict);
  auto* retentionSummary = warning(retention, QString());
  const auto updateRetentionWizard = [cleanupEnabled, retentionEnabled, retentionHours, retentionStrict, retentionSummary]() {
    const bool available = cleanupEnabled->isChecked();
    retentionEnabled->setEnabled(available);
    retentionHours->setEnabled(available && retentionEnabled->isChecked());
    retentionStrict->setEnabled(available && retentionEnabled->isChecked());
    retentionSummary->setText(!available
      ? QObject::tr("CLEANUP IS OFF — enable cleanup on the previous page before a retention deadline can run.")
      : !retentionEnabled->isChecked()
        ? QObject::tr("MAXIMUM RETENTION IS OFF — torrents will not be removed merely because of their age.")
        : QObject::tr("TIME-BASED CLEANUP IS ON — completed managed torrents become due after %1 hours (%2 days), even when free space is healthy.")
            .arg(retentionHours->value()).arg(retentionHours->value() / 24.0, 0, 'f', 1));
  };
  connect(cleanupEnabled, &QCheckBox::toggled, &wizard, updateRetentionWizard);
  connect(retentionEnabled, &QCheckBox::toggled, &wizard, updateRetentionWizard);
  connect(retentionHours, QOverload<int>::of(&QSpinBox::valueChanged), &wizard, updateRetentionWizard);
  updateRetentionWizard();
  note(retention, tr("<b>Example:</b> 720 hours means 30 days. With a firm deadline, a torrent is due at 30 days even if its ratio is low or it is still uploading. A <i>keep</i> tag, protected tracker or minimum-copy rule can still retain it.<br><br>"
                     "If <b>Delete downloaded data</b> is off, only the torrent job is removed and its files stay on disk. If it is on, the files are permanently deleted too."));
  detailedHelp.insert(wizard.pageIds().constLast(), tr("Maximum-retention cleanup is independent of storage pressure and is checked approximately every five minutes while RSS Guard is running. Age is measured from completion, or from the client-reported added time when completion time is unavailable. A firm deadline overrides ratio, inactivity, current-upload and recent-upload postponements. Protected tags, protected tracker text and minimum-copy protection remain absolute. The cleanup time window, per-run removal limit, confirmation choice and downloaded-data choice still apply."));
  retention->layout()->addItem(new QSpacerItem(1, 1, QSizePolicy::Minimum, QSizePolicy::Expanding));

  QWizardPage* protection = page(tr("9. Cleanup protections and window"),
    tr("Protect active, recently added or specially labelled torrents, and optionally restrict cleanup to a maintenance window."));
  auto* protectionForm = form(protection);
  auto* protectUploading = check(protection, tr("Protect torrents currently uploading"), m_protectUploading->isChecked(), tr("Skips torrents at or above the upload threshold."));
  auto* uploadKib = integer(protection, m_protectUploadKib->value(), 1, 1048576, tr(" KiB/s"), tr("Per-torrent upload-speed threshold."));
  auto* unknownSpeed = check(protection, tr("Protect when upload speed is unavailable"), m_protectUnknownSpeed->isChecked(), tr("Safest choice for limited client APIs."));
  auto* recentUpload = integer(protection, m_protectRecentHours->value(), 0, 100000, tr(" hours"), tr("Protect after RSS Guard last observed upload activity; zero disables."));
  auto* grace = check(protection, tr("Require a cleanup grace period"), m_cleanupGrace->isChecked(), tr("A candidate must remain eligible through a later check."));
  auto* graceHours = integer(protection, m_cleanupGraceHours->value(), 1, 100000, tr(" hours"), tr("Minimum delay between candidate marking and removal."));
  auto* smartOrder = check(protection, tr("Use smart cleanup ordering"), m_smartCleanup->isChecked(), tr("Ranks by age, recoverable size and ratio."));
  auto* copiesEnabled = check(protection, tr("Keep completed copies across clients"), m_minimumCopiesEnabled->isChecked(), tr("Prevents cleanup from removing the last required completed copy."));
  auto* copies = integer(protection, m_minimumCopies->value(), 1, 100, QString(), tr("Minimum completed copies retained."));
  auto* tags = new QLineEdit(m_protectedTags->text(), protection); tags->setToolTip(tr("Comma-separated exact tags, for example: keep, archive"));
  auto* trackers = new QLineEdit(m_protectedTrackers->text(), protection); trackers->setToolTip(tr("Comma-separated tracker text, for example: tracker.example"));
  auto* cleanupSchedule = check(protection, tr("Restrict cleanup to set hours"), m_cleanupSchedule->isChecked(), tr("Outside the window, cleanup waits."));
  auto* cleanupStart = hours(protection, m_cleanupScheduleStart->currentData().toInt(), false);
  auto* cleanupEnd = hours(protection, m_cleanupScheduleEnd->currentData().toInt(), true);
  auto* cleanupWindow = new QWidget(protection); auto* cleanupWindowLayout = new QHBoxLayout(cleanupWindow);
  cleanupWindowLayout->setContentsMargins(0, 0, 0, 0); cleanupWindowLayout->addWidget(cleanupStart); cleanupWindowLayout->addWidget(new QLabel(tr("to"), cleanupWindow)); cleanupWindowLayout->addWidget(cleanupEnd);
  auto* batch = decimal(protection, m_cleanupBatchPercent->value(), 0.0, 100.0, 1, tr(" %"), tr("Rounds the required space target upward; zero disables batch rounding."));
  protectionForm->addRow(protectUploading, uploadKib); protectionForm->addRow(unknownSpeed);
  protectionForm->addRow(tr("Protect after recent upload:"), recentUpload); protectionForm->addRow(grace, graceHours);
  protectionForm->addRow(smartOrder); protectionForm->addRow(copiesEnabled, copies);
  protectionForm->addRow(tr("Never remove tags:"), tags); protectionForm->addRow(tr("Never remove tracker text:"), trackers);
  protectionForm->addRow(cleanupSchedule, cleanupWindow); protectionForm->addRow(tr("Cleanup space batch:"), batch);
  const auto updateProtectionEditors = [protectUploading, uploadKib, unknownSpeed, grace, graceHours,
                                         copiesEnabled, copies, cleanupSchedule, cleanupStart, cleanupEnd]() {
    uploadKib->setEnabled(protectUploading->isChecked()); unknownSpeed->setEnabled(protectUploading->isChecked());
    graceHours->setEnabled(grace->isChecked()); copies->setEnabled(copiesEnabled->isChecked());
    cleanupStart->setEnabled(cleanupSchedule->isChecked()); cleanupEnd->setEnabled(cleanupSchedule->isChecked());
  };
  connect(protectUploading, &QCheckBox::toggled, &wizard, updateProtectionEditors);
  connect(grace, &QCheckBox::toggled, &wizard, updateProtectionEditors);
  connect(copiesEnabled, &QCheckBox::toggled, &wizard, updateProtectionEditors);
  connect(cleanupSchedule, &QCheckBox::toggled, &wizard, updateProtectionEditors); updateProtectionEditors();
  note(protection, tr("<b>Safest starting point:</b> protect active/unknown uploads, keep the grace period, keep smart ordering, add any personal "
                      "<i>keep</i> or <i>archive</i> tags, and leave downloaded-data deletion off on the previous page."));
  detailedHelp.insert(wizard.pageIds().constLast(), tr("Current-upload protection skips a torrent above the selected KiB/s speed. Unknown-speed protection keeps torrents safe when an API cannot report that value. Recent-upload protection remembers previously observed uploads. A grace period marks a candidate and waits before a later recheck. Smart ordering favours safer cleanup choices. Copy protection can keep completed duplicates across clients. Protected tags and tracker text are absolute exclusions. A cleanup window delays removal until chosen local hours. Batch percentage rounds the storage target upward to avoid repeated small cleanup runs."));

  QWizardPage* finish = page(tr("10. Review and finish"), tr("Press Finish to copy these choices into Torrent automation settings."));
  const int finishPageId = wizard.pageIds().constLast();
  note(finish, tr("After finishing, press <b>Apply</b> or <b>OK</b> in the main Settings window to save everything permanently.<br><br>"
                  "Before live use: test every enabled client, run a dry test, inspect Simple and Activity, and use Check readiness for live automation. "
                  "The wizard never sends a torrent or performs cleanup."));
  auto* finishDry = new QLabel(finish); finishDry->setWordWrap(true); finish->layout()->addWidget(finishDry);
  connect(&wizard, &QWizard::currentIdChanged, &wizard, [finishPageId, finishDry, dryRun, cleanupEnabled, deleteData, retentionEnabled, retentionHours](int id) {
    if (id != finishPageId) return;
    finishDry->setText(QObject::tr("<b>Selected safety state:</b> Dry run: %1 · Cleanup: %2 · Delete downloaded data: %3 · Maximum retention: %4")
      .arg(dryRun->isChecked() ? QObject::tr("ON") : QObject::tr("OFF"),
           cleanupEnabled->isChecked() ? QObject::tr("ON") : QObject::tr("OFF"),
           deleteData->isChecked() ? QObject::tr("ON") : QObject::tr("OFF"),
           retentionEnabled->isChecked() ? QObject::tr("%1 hours").arg(retentionHours->value()) : QObject::tr("OFF")));
  });
  finish->layout()->addItem(new QSpacerItem(1, 1, QSizePolicy::Minimum, QSizePolicy::Expanding));
  detailedHelp.insert(finishPageId, tr("Finish copies the answers back to the visible Torrent automation settings page. It deliberately does not save, send or clean up anything. Apply or OK performs the save. Run dry test now produces a read-only example, and Check readiness for live automation reports anything that should be corrected before live mode."));

  connect(wizardPresetApply, &QPushButton::clicked, &wizard,
          [&wizard, wizardPresetChoice, wizardPresetApply, enabled, dryRun, notifications, strategy,
           retryEnabled, retryAttempts, retryInitial, retryMaximum, retryBackoff, requestTimeout,
           reconciliation, reconciliationMinutes, reserve, duplicates, breaker, breakerFailures,
           breakerCooldown, breakerRecovery, cleanupEnabled, deleteData, confirmCleanup,
           seedEnabled, seedHours, ratioEnabled, ratio, inactiveEnabled, inactiveHours,
           maxEnabled, maxRemovals, stopEnabled, stopSpace, protectUploading, uploadKib,
           unknownSpeed, recentUpload, grace, graceHours, smartOrder, batch,
           retentionEnabled, retentionHours, retentionStrict]() {
    const int preset = wizardPresetChoice->currentData().toInt();
    if (preset == PresetCustom) return;
    QMessageBox review(&wizard);
    review.setWindowTitle(QObject::tr("Review Quick Set preset"));
    review.setIcon(preset == PresetSafeTest ? QMessageBox::Information : QMessageBox::Warning);
    review.setText(quickPresetName(preset));
    review.setInformativeText(quickPresetDetail(preset, false));
    auto* apply = review.addButton(QObject::tr("Use this preset"), QMessageBox::AcceptRole);
    review.addButton(QMessageBox::Cancel);
    review.exec();
    if (review.clickedButton() != apply) return;

    enabled->setChecked(true); dryRun->setChecked(true); notifications->setChecked(true);
    strategy->setCurrentIndex(strategy->findData(static_cast<int>(TorrentRoutingStrategy::Balanced)));
    retryEnabled->setChecked(true); retryAttempts->setValue(3); retryInitial->setValue(60);
    retryMaximum->setValue(900); retryBackoff->setChecked(true); requestTimeout->setValue(15);
    reconciliation->setChecked(true); reconciliationMinutes->setValue(30); reserve->setChecked(true);
    duplicates->setChecked(true); breaker->setChecked(true); breakerFailures->setValue(3);
    breakerCooldown->setValue(15); breakerRecovery->setValue(2);

    if (preset == PresetSafeTest) {
      cleanupEnabled->setChecked(false); deleteData->setChecked(false); confirmCleanup->setChecked(true);
      retentionEnabled->setChecked(false);
    }
    else {
      cleanupEnabled->setChecked(true); deleteData->setChecked(true); confirmCleanup->setChecked(true);
      seedEnabled->setChecked(true); ratioEnabled->setChecked(true); inactiveEnabled->setChecked(true);
      maxEnabled->setChecked(true); maxRemovals->setValue(1); stopEnabled->setChecked(true);
      stopSpace->setValue(40.0); protectUploading->setChecked(true); uploadKib->setValue(256);
      unknownSpeed->setChecked(true); recentUpload->setValue(24); grace->setChecked(true);
      smartOrder->setChecked(true); batch->setValue(5.0);
      if (preset == PresetBalanced) {
        seedHours->setValue(168); ratio->setValue(1.0); inactiveHours->setValue(24);
        graceHours->setValue(24); retentionEnabled->setChecked(false);
        retentionHours->setValue(720); retentionStrict->setChecked(true);
      }
      else if (preset == PresetThirtyDay) {
        seedHours->setValue(168); ratio->setValue(1.0); inactiveHours->setValue(24);
        graceHours->setValue(24); retentionEnabled->setChecked(true);
        retentionHours->setValue(720); retentionStrict->setChecked(true);
      }
      else if (preset == PresetQuickTurnaround) {
        seedHours->setValue(72); ratio->setValue(0.5); inactiveHours->setValue(6);
        stopSpace->setValue(60.0); recentUpload->setValue(6); graceHours->setValue(6);
        maxRemovals->setValue(3); retentionEnabled->setChecked(true);
        retentionHours->setValue(168); retentionStrict->setChecked(true);
      }
      else if (preset == PresetLongSeed) {
        seedHours->setValue(720); ratio->setValue(2.0); inactiveHours->setValue(72);
        graceHours->setValue(48); retentionEnabled->setChecked(true);
        retentionHours->setValue(2160); retentionStrict->setChecked(false);
      }
    }
    wizardPresetApply->setText(QObject::tr("Preset applied — review next pages"));
  });

  connect(&wizard, &QWizard::helpRequested, &wizard, [&wizard, detailedHelp]() {
    QMessageBox::information(&wizard, QObject::tr("About this setup step"),
      detailedHelp.value(wizard.currentId(), QObject::tr("Review the choices on this page. Hover over an individual option for its specific explanation and example.")));
  });

  if (wizard.exec() != QDialog::Accepted) {
    m_config.rules = originalRules;
    refreshRules();
    return;
  }

  m_enabled->setChecked(enabled->isChecked()); m_dryRun->setChecked(dryRun->isChecked());
  m_notifications->setChecked(notifications->isChecked()); m_historyLimit->setValue(history->value());
  m_strategy->setCurrentIndex(m_strategy->findData(strategy->currentData()));
  m_unknownSizeGb->setValue(unknownSize->value());
  m_storageUnit->setCurrentIndex(m_storageUnit->findData(storageUnit->currentData()));
  for (int row = 0; row < m_clients->rowCount(); ++row)
    for (int column = 0; column < m_clients->columnCount(); ++column)
      if (clientTable->item(row, column) != nullptr && m_clients->item(row, column) != nullptr)
        *m_clients->item(row, column) = *clientTable->item(row, column);
  m_retryEnabled->setChecked(retryEnabled->isChecked()); m_retryAttempts->setValue(retryAttempts->value());
  m_retryInitialSeconds->setValue(retryInitial->value()); m_retryMaximumSeconds->setValue(retryMaximum->value());
  m_retryBackoff->setChecked(retryBackoff->isChecked()); m_requestTimeoutSeconds->setValue(requestTimeout->value());
  m_reconciliation->setChecked(reconciliation->isChecked()); m_reserveRemaining->setChecked(reserve->isChecked());
  m_preventDuplicates->setChecked(duplicates->isChecked()); m_reconciliationMinutes->setValue(reconciliationMinutes->value());
  m_circuitBreaker->setChecked(breaker->isChecked()); m_breakerFailures->setValue(breakerFailures->value());
  m_breakerCooldown->setValue(breakerCooldown->value()); m_breakerRecoverySuccesses->setValue(breakerRecovery->value());
  m_schedule->setChecked(schedule->isChecked()); m_scheduleStart->setCurrentIndex(m_scheduleStart->findData(scheduleStart->currentData()));
  m_scheduleEnd->setCurrentIndex(m_scheduleEnd->findData(scheduleEnd->currentData()));
  m_cleanup->setChecked(cleanupEnabled->isChecked()); m_deleteData->setChecked(deleteData->isChecked());
  m_confirmCleanup->setChecked(confirmCleanup->isChecked()); m_seedHoursEnabled->setChecked(seedEnabled->isChecked());
  m_retentionEnabled->setChecked(retentionEnabled->isChecked()); m_retentionHours->setValue(retentionHours->value());
  m_retentionStrict->setChecked(retentionStrict->isChecked());
  m_seedHours->setValue(seedHours->value()); m_ratioEnabled->setChecked(ratioEnabled->isChecked()); m_ratio->setValue(ratio->value());
  m_inactiveHoursEnabled->setChecked(inactiveEnabled->isChecked()); m_inactiveHours->setValue(inactiveHours->value());
  m_maxRemovalsEnabled->setChecked(maxEnabled->isChecked()); m_maxRemovals->setValue(maxRemovals->value());
  m_cleanupStopGbEnabled->setChecked(stopEnabled->isChecked()); m_cleanupStopGb->setValue(stopSpace->value());
  m_protectUploading->setChecked(protectUploading->isChecked()); m_protectUploadKib->setValue(uploadKib->value());
  m_protectUnknownSpeed->setChecked(unknownSpeed->isChecked()); m_protectRecentHours->setValue(recentUpload->value());
  m_cleanupGrace->setChecked(grace->isChecked()); m_cleanupGraceHours->setValue(graceHours->value());
  m_smartCleanup->setChecked(smartOrder->isChecked()); m_minimumCopiesEnabled->setChecked(copiesEnabled->isChecked());
  m_minimumCopies->setValue(copies->value()); m_protectedTags->setText(tags->text()); m_protectedTrackers->setText(trackers->text());
  m_cleanupSchedule->setChecked(cleanupSchedule->isChecked());
  m_cleanupScheduleStart->setCurrentIndex(m_cleanupScheduleStart->findData(cleanupStart->currentData()));
  m_cleanupScheduleEnd->setCurrentIndex(m_cleanupScheduleEnd->findData(cleanupEnd->currentData()));
  m_cleanupBatchPercent->setValue(batch->value());
  refreshRules(); updateCleanupControls(); dirtifySettings();
  QMessageBox::information(this, tr("Setup wizard complete"),
    tr("The wizard choices are now shown in Torrent automation settings. Press Apply or OK to save them, then run the dry test and readiness check before disabling Dry run."));
}

void SettingsTorrentAutomation::runDryTest() {
  TorrentAutomationEngine::instance(qApp)->runDryTest();
}

void SettingsTorrentAutomation::runReadinessAudit() {
  saveSettings();
  m_config = TorrentAutomationConfig::load(settings());
  const QList<TorrentClientConfig> allClients = TorrentClientConfig::load(settings());
  QStringList errors, warnings, passed, participatingIds;
  int cleanupClients = 0;
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
    if (policy.allowCleanup) ++cleanupClients;
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
    if (cleanupClients == 0) errors.append(tr("Cleanup is enabled, but no participating client permits safe cleanup."));
    if (m_config.deleteData) warnings.append(tr("Cleanup can permanently delete downloaded data."));
    else if (m_config.maximumRetentionEnabled)
      warnings.append(tr("Maximum-retention cleanup will remove torrent jobs but keep their downloaded files, so it will not recover disk space."));
    if (!m_config.cleanupRequireConfirmation) warnings.append(tr("Per-removal confirmation is disabled."));
    if (!m_config.cleanupGraceEnabled) warnings.append(tr("The cleanup grace period is disabled."));
    if (!m_config.maximumRemovalsEnabled) warnings.append(tr("The user-defined removal limit is disabled; the internal limit of 25 still applies."));
    if (!m_config.minimumSeedHoursEnabled && !m_config.minimumRatioEnabled && !m_config.minimumInactiveHoursEnabled)
      warnings.append(tr("All age, ratio and inactivity cleanup filters are disabled."));
    if (m_config.maximumRetentionEnabled) {
      passed.append(tr("Maximum-retention cleanup is enabled at %1 hours and runs independently of free-space pressure.")
                      .arg(m_config.maximumRetentionHours));
      if (m_config.maximumRetentionStrict)
        warnings.append(tr("The maximum-retention deadline overrides ratio, inactivity and upload-activity protections after expiry."));
    }
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
                               {QStringLiteral("username"), client.username},
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
  const QJsonObject clientSettings{
    {QStringLiteral("showSuccessNotifications"),
     settings()->value(QStringLiteral("TorrentClients"), QStringLiteral("showSuccessNotifications"), true).toBool()}};
  const QJsonObject root{{QStringLiteral("format"), QStringLiteral("rssguard-torrent-configuration-v2")},
                         {QStringLiteral("exportedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                         {QStringLiteral("credentialsIncluded"), false},
                         {QStringLiteral("clientSettings"), clientSettings},
                         {QStringLiteral("clients"), clients},
                         {QStringLiteral("automation"), automation}};
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
    QMessageBox::warning(this, tr("Export failed"), tr("The configuration file could not be written."));
    return;
  }
  QMessageBox::information(this, tr("Configuration exported"),
                           tr("All portable torrent settings were exported. Passwords and API tokens were deliberately "
                              "excluded. Use the app-wide Backup settings feature when an encrypted, complete transfer "
                              "including credentials and automation state is required."));
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
  const QString format = root.value(QStringLiteral("format")).toString();
  if (parseError.error != QJsonParseError::NoError ||
      (format != QStringLiteral("rssguard-torrent-configuration-v1") &&
       format != QStringLiteral("rssguard-torrent-configuration-v2")) ||
      !root.value(QStringLiteral("automation")).isObject()) {
    QMessageBox::warning(this, tr("Import failed"), tr("This is not a supported RSS Guard torrent-configuration file."));
    return;
  }
  if (QMessageBox::warning(this, tr("Replace torrent configuration"),
      tr("Replace every portable torrent setting with this file? This includes clients, destinations, colours, "
         "notifications, limits, rules, routing, retries, schedules, storage, cleanup, retention and protections. "
         "Existing passwords and tokens are retained only for clients with matching IDs."),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;

  const QList<TorrentClientConfig> existing = TorrentClientConfig::load(settings());
  QList<TorrentClientConfig> imported;
  for (const QJsonValue& value : root.value(QStringLiteral("clients")).toArray()) {
    const QJsonObject object = value.toObject();
    TorrentClientConfig client;
    client.id = object.value(QStringLiteral("id")).toString();
    QString previousTypeAndEndpoint;
    for (const TorrentClientConfig& current : existing)
      if (current.id == client.id) {
        client = current;
        previousTypeAndEndpoint = QStringLiteral("%1|%2")
                                    .arg(static_cast<int>(current.type))
                                    .arg(current.baseUrl.trimmed());
        break;
      }
    client.id = object.value(QStringLiteral("id")).toString();
    client.name = object.value(QStringLiteral("name")).toString();
    client.type = static_cast<TorrentClientType>(qBound(0, object.value(QStringLiteral("type")).toInt(), 6));
    client.baseUrl = object.value(QStringLiteral("baseUrl")).toString();
    if (object.contains(QStringLiteral("username"))) client.username = object.value(QStringLiteral("username")).toString();
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
    const QString importedTypeAndEndpoint = QStringLiteral("%1|%2")
                                             .arg(static_cast<int>(client.type))
                                             .arg(client.baseUrl.trimmed());
    if (previousTypeAndEndpoint.isEmpty() || previousTypeAndEndpoint != importedTypeAndEndpoint) {
      client.capabilityTested = false;
      client.capabilityConnected = false;
      client.capabilityLiveStatus = false;
      client.capabilityFreeSpace = false;
      client.capabilityTorrentList = false;
      client.capabilityTransferRates = false;
      client.capabilityRemoval = false;
      client.capabilityTestedAt = {};
      client.capabilityDetail.clear();
    }
    if (!client.id.isEmpty() && !client.name.isEmpty() && !client.baseUrl.isEmpty()) imported.append(client);
  }
  TorrentClientConfig::save(settings(), imported);
  if (format == QStringLiteral("rssguard-torrent-configuration-v2") &&
      root.value(QStringLiteral("clientSettings")).isObject()) {
    const QJsonObject clientSettings = root.value(QStringLiteral("clientSettings")).toObject();
    settings()->setValue(QStringLiteral("TorrentClients"), QStringLiteral("showSuccessNotifications"),
                         clientSettings.value(QStringLiteral("showSuccessNotifications")).toBool(true));
  }
  settings()->setValue(QStringLiteral("TorrentAutomation"), QStringLiteral("configuration"),
                       QJsonDocument(root.value(QStringLiteral("automation")).toObject()).toJson(QJsonDocument::Compact));
  settings()->sync();
  loadSettings();
  dirtifySettings();
  QMessageBox::information(this, tr("Configuration imported"),
                           tr("Every portable torrent section was imported. Enter credentials for newly imported clients "
                              "and run Test all enabled before using automation. Clients whose type or address changed "
                              "must be capability-tested again."));
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
  const QList<QWidget*> cleanup_widgets = {m_deleteData, m_confirmCleanup, m_retentionEnabled,
                                            m_retentionStrict, m_seedHoursEnabled, m_ratioEnabled,
                                            m_inactiveHoursEnabled, m_maxRemovalsEnabled, m_cleanupStopGbEnabled,
                                            m_protectUploading, m_protectUnknownSpeed, m_protectRecentHours,
                                            m_cleanupGrace, m_smartCleanup, m_minimumCopiesEnabled,
                                            m_protectedTags, m_protectedTrackers, m_cleanupSchedule,
                                            m_cleanupBatchPercent};
  for (QWidget* widget : cleanup_widgets) widget->setEnabled(enabled);
  m_retentionHours->setEnabled(enabled && m_retentionEnabled->isChecked());
  m_retentionStrict->setEnabled(enabled && m_retentionEnabled->isChecked());
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
