// For license of this file, see <project-root-folder>/LICENSE.md.

#ifndef SETTINGSTORRENTAUTOMATION_H
#define SETTINGSTORRENTAUTOMATION_H

#include "gui/settings/settingspanel.h"
#include "torrent/torrentautomationconfig.h"
#include "torrent/torrentclientconfig.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;

class SettingsTorrentAutomation final : public SettingsPanel {
    Q_OBJECT

  public:
    explicit SettingsTorrentAutomation(Settings* settings, QWidget* parent = nullptr);
    QString title() const override;
    QIcon icon() const override;
    void loadUi() override;
    void loadSettings() override;
    void saveSettings() override;

  private slots:
    void addRule();
    void editRule();
    void duplicateRule();
    void moveRuleUp();
    void moveRuleDown();
    void removeRule();
    void refreshRules();
    void refreshActivity();
    void refreshSimpleActivity();
    void updateCleanupControls();
    void updateCapabilityDisplay();
    void testSelectedClient();
    void testAllClients();
    void testNextClient();
    void runDryTest();
    void runReadinessAudit();
    void exportConfiguration();
    void importConfiguration();
    void retryQueuedItem();
    void retryAllQueuedItems();
    void chooseQueuedClient();
    void cancelQueuedItem();
    void cancelAllQueuedItems();
    void exportActivity();
    void clearActivity();
    void changeStorageUnit(int index);
    void runSetupWizard();

  private:
    TorrentAutomationRule editRuleDialog(const TorrentAutomationRule& initial, bool* accepted);
    void refreshClientPolicies();
    void storeCapabilityResult(const TorrentClientConfig& tested,
                               bool connected,
                               bool liveStatus,
                               bool freeSpace,
                               bool torrentList,
                               bool transferRates,
                               bool removal,
                               qint64 totalBytes,
                               const QString& detail);

    TorrentAutomationConfig m_config;
    QCheckBox *m_enabled = nullptr, *m_dryRun = nullptr, *m_notifications = nullptr;
    QComboBox *m_strategy = nullptr, *m_storageUnit = nullptr;
    QString m_currentStorageUnit = QStringLiteral("GiB");
    QSpinBox* m_historyLimit = nullptr;
    QDoubleSpinBox* m_unknownSizeGb = nullptr;
    QCheckBox *m_retryEnabled = nullptr, *m_retryBackoff = nullptr;
    QSpinBox *m_retryAttempts = nullptr, *m_retryInitialSeconds = nullptr,
             *m_retryMaximumSeconds = nullptr, *m_requestTimeoutSeconds = nullptr;
    QCheckBox *m_reconciliation = nullptr, *m_reserveRemaining = nullptr, *m_preventDuplicates = nullptr,
              *m_circuitBreaker = nullptr, *m_schedule = nullptr;
    QSpinBox *m_reconciliationMinutes = nullptr, *m_breakerFailures = nullptr,
             *m_breakerCooldown = nullptr, *m_breakerRecoverySuccesses = nullptr;
    QComboBox *m_scheduleStart = nullptr, *m_scheduleEnd = nullptr;
    QTableWidget* m_clients = nullptr;
    QList<TorrentClientConfig> m_clientConfigs, m_testQueue;
    QCheckBox *m_capConnected = nullptr, *m_capStatus = nullptr, *m_capSpace = nullptr,
              *m_capList = nullptr, *m_capRates = nullptr, *m_capRemoval = nullptr;
    QLabel* m_capabilityTested = nullptr;
    QPushButton *m_testSelected = nullptr, *m_testAll = nullptr, *m_runDryTest = nullptr;
    QStringList m_testResults;
    int m_testFailures = 0;
    QListWidget *m_rules = nullptr, *m_activity = nullptr, *m_simpleActivity = nullptr;
    QListWidget* m_queue = nullptr;
    QPushButton *m_editRule = nullptr, *m_removeRule = nullptr;
    QCheckBox *m_cleanup = nullptr, *m_deleteData = nullptr, *m_confirmCleanup = nullptr;
    QCheckBox *m_seedHoursEnabled = nullptr, *m_ratioEnabled = nullptr, *m_inactiveHoursEnabled = nullptr,
              *m_maxRemovalsEnabled = nullptr, *m_cleanupStopGbEnabled = nullptr;
    QSpinBox *m_seedHours = nullptr, *m_inactiveHours = nullptr, *m_maxRemovals = nullptr;
    QDoubleSpinBox *m_ratio = nullptr, *m_cleanupStopGb = nullptr;
    QCheckBox *m_protectUploading = nullptr, *m_protectUnknownSpeed = nullptr;
    QSpinBox *m_protectUploadKib = nullptr, *m_protectRecentHours = nullptr,
             *m_cleanupGraceHours = nullptr, *m_minimumCopies = nullptr;
    QCheckBox *m_cleanupGrace = nullptr, *m_smartCleanup = nullptr,
              *m_minimumCopiesEnabled = nullptr, *m_cleanupSchedule = nullptr;
    QComboBox *m_cleanupScheduleStart = nullptr, *m_cleanupScheduleEnd = nullptr;
    QLineEdit *m_protectedTags = nullptr, *m_protectedTrackers = nullptr;
    QDoubleSpinBox* m_cleanupBatchPercent = nullptr;
};

#endif // SETTINGSTORRENTAUTOMATION_H
