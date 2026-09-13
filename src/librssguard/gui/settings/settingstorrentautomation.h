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
    void removeRule();
    void refreshRules();
    void refreshActivity();
    void updateCleanupControls();
    void updateCapabilityDisplay();
    void testSelectedClient();
    void testAllClients();
    void testNextClient();
    void runDryTest();

  private:
    TorrentAutomationRule editRuleDialog(const TorrentAutomationRule& initial, bool* accepted);
    void refreshClientPolicies();
    void storeCapabilityResult(const TorrentClientConfig& tested,
                               bool connected,
                               bool liveStatus,
                               bool freeSpace,
                               bool torrentList,
                               bool removal,
                               qint64 totalBytes,
                               const QString& detail);

    TorrentAutomationConfig m_config;
    QCheckBox *m_enabled = nullptr, *m_dryRun = nullptr, *m_notifications = nullptr;
    QComboBox* m_strategy = nullptr;
    QSpinBox *m_retry = nullptr, *m_historyLimit = nullptr;
    QDoubleSpinBox* m_unknownSizeGb = nullptr;
    QTableWidget* m_clients = nullptr;
    QList<TorrentClientConfig> m_clientConfigs, m_testQueue;
    QCheckBox *m_capConnected = nullptr, *m_capStatus = nullptr, *m_capSpace = nullptr,
              *m_capList = nullptr, *m_capRemoval = nullptr;
    QLabel* m_capabilityTested = nullptr;
    QPushButton *m_testSelected = nullptr, *m_testAll = nullptr, *m_runDryTest = nullptr;
    QStringList m_testResults;
    int m_testFailures = 0;
    QListWidget *m_rules = nullptr, *m_activity = nullptr;
    QPushButton *m_editRule = nullptr, *m_removeRule = nullptr;
    QCheckBox *m_cleanup = nullptr, *m_deleteData = nullptr, *m_confirmCleanup = nullptr;
    QCheckBox *m_seedHoursEnabled = nullptr, *m_ratioEnabled = nullptr, *m_inactiveHoursEnabled = nullptr,
              *m_maxRemovalsEnabled = nullptr, *m_cleanupStopGbEnabled = nullptr;
    QSpinBox *m_seedHours = nullptr, *m_inactiveHours = nullptr, *m_maxRemovals = nullptr;
    QDoubleSpinBox *m_ratio = nullptr, *m_cleanupStopGb = nullptr;
};

#endif // SETTINGSTORRENTAUTOMATION_H
