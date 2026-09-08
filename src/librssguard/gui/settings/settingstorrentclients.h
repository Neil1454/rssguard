// For license of this file, see <project-root-folder>/LICENSE.md.

#ifndef SETTINGSTORRENTCLIENTS_H
#define SETTINGSTORRENTCLIENTS_H

#include "gui/settings/settingspanel.h"
#include "torrent/torrentclientconfig.h"

class QLabel;
class QListWidget;
class QPushButton;

class SettingsTorrentClients : public SettingsPanel {
    Q_OBJECT

  public:
    explicit SettingsTorrentClients(Settings* settings, QWidget* parent = nullptr);
    QString title() const override;
    QIcon icon() const override;
    void loadUi() override;
    void loadSettings() override;
    void saveSettings() override;

  private slots:
    void addClient();
    void editClient();
    void removeClient();
    void testClient();
    void updateButtons();

  private:
    void refreshList(int selected = -1);
    int selectedIndex() const;

    QList<TorrentClientConfig> m_clients;
    QListWidget* m_list = nullptr;
    QLabel* m_description = nullptr;
    QPushButton* m_edit = nullptr;
    QPushButton* m_remove = nullptr;
    QPushButton* m_test = nullptr;
};

#endif // SETTINGSTORRENTCLIENTS_H
