// For license of this file, see <project-root-folder>/LICENSE.md.

#ifndef SETTINGSTORRENTAUTOMATION_H
#define SETTINGSTORRENTAUTOMATION_H

#include "gui/settings/settingspanel.h"
#include "torrent/torrentautomationconfig.h"

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

  private:
    TorrentAutomationRule editRuleDialog(const TorrentAutomationRule& initial, bool* accepted);
    void refreshClientPolicies();

    TorrentAutomationConfig m_config;
    QCheckBox *m_enabled = nullptr, *m_dryRun = nullptr, *m_notifications = nullptr;
    QComboBox* m_strategy = nullptr;
    QSpinBox *m_retry = nullptr, *m_historyLimit = nullptr;
    QTableWidget* m_clients = nullptr;
    QListWidget *m_rules = nullptr, *m_activity = nullptr;
    QPushButton *m_editRule = nullptr, *m_removeRule = nullptr;
    QCheckBox *m_cleanup = nullptr, *m_deleteData = nullptr, *m_confirmCleanup = nullptr;
    QSpinBox *m_seedHours = nullptr, *m_inactiveHours = nullptr, *m_maxRemovals = nullptr;
    QDoubleSpinBox *m_ratio = nullptr, *m_cleanupStopGb = nullptr;
};

#endif // SETTINGSTORRENTAUTOMATION_H
