// For license of this file, see <project-root-folder>/LICENSE.md.

#include "gui/settings/settingstorrentclients.h"

#include "miscellaneous/application.h"
#include "miscellaneous/iconfactory.h"
#include "miscellaneous/settings.h"
#include "torrent/torrentclient.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

#include <utility>

namespace {
  class TorrentClientEditor final : public QDialog {
    public:
      explicit TorrentClientEditor(const TorrentClientConfig& initial, QWidget* parent) : QDialog(parent), m_config(initial) {
        setWindowTitle(initial.id.isEmpty() ? tr("Add torrent client") : tr("Edit torrent client"));
        auto* outer = new QVBoxLayout(this);
        m_form = new QFormLayout();
        m_name = new QLineEdit(initial.name, this);
        m_type = new QComboBox(this);
        for (int i = 0; i < 5; ++i) m_type->addItem(TorrentClientConfig::typeName(static_cast<TorrentClientType>(i)), i);
        m_type->setCurrentIndex(static_cast<int>(initial.type));
        m_url = new QLineEdit(initial.baseUrl, this);
        m_username = new QLineEdit(initial.username, this);
        m_password = new QLineEdit(initial.password, this);
        m_password->setEchoMode(QLineEdit::Password);
        m_token = new QLineEdit(initial.token, this);
        m_token->setEchoMode(QLineEdit::Password);
        m_proxy = new QCheckBox(tr("Use RSS Guard proxy"), this);
        m_proxy->setChecked(initial.useRssGuardProxy);
        m_default = new QCheckBox(tr("Default torrent client"), this);
        m_default->setChecked(initial.isDefault);
        m_path = new QLineEdit(initial.savePath, this);
        m_category = new QLineEdit(initial.category, this);
        m_tags = new QLineEdit(initial.tags.join(QStringLiteral(", ")), this);
        m_form->addRow(tr("Name:"), m_name);
        m_form->addRow(tr("Client type:"), m_type);
        m_form->addRow(tr("Server/base URL:"), m_url);
        m_form->addRow(tr("Username:"), m_username);
        m_form->addRow(tr("Password:"), m_password);
        m_form->addRow(tr("Flood token (optional):"), m_token);
        m_form->addRow(QString(), m_proxy);
        m_form->addRow(QString(), m_default);
        m_form->addRow(tr("Default save path (optional):"), m_path);
        m_form->addRow(tr("Category/label (optional):"), m_category);
        m_form->addRow(tr("Tags, comma-separated (optional):"), m_tags);
        m_url->setToolTip(tr("Enter the web or RPC address for the selected client. The example changes with Client type."));
        m_username->setToolTip(tr("Login username. Disabled when the selected client does not use one."));
        m_password->setToolTip(tr("Password used by the client's Web UI or RPC service."));
        m_token->setToolTip(tr("Flood only: an existing JWT token can be used instead of username and password."));
        m_path->setToolTip(tr("A directory on the torrent server, not necessarily a folder on this computer."));
        m_category->setToolTip(tr("qBittorrent category, or rTorrent custom label. Disabled for clients that do not support it here."));
        m_tags->setToolTip(tr("Comma-separated qBittorrent/Flood tags or Transmission labels. Disabled for unsupported clients."));
        m_path->setPlaceholderText(tr("Example: /downloads/rss"));
        m_category->setPlaceholderText(tr("Example: tv"));
        m_tags->setPlaceholderText(tr("Example: rss, automatic"));
        connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { updateClientFields(); });
        connect(m_token, &QLineEdit::textChanged, this, [this]() { updateClientFields(); });
        updateClientFields();
        outer->addLayout(m_form);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
          TorrentClientConfig candidate = value();
          QString error;
          if (!candidate.isValid(&error)) { QMessageBox::warning(this, tr("Invalid torrent client"), error); return; }
          accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        outer->addWidget(buttons);
        resize(560, sizeHint().height());
      }

      TorrentClientConfig value() const {
        TorrentClientConfig result = m_config;
        if (result.id.isEmpty()) result.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        result.name = m_name->text().trimmed();
        result.type = static_cast<TorrentClientType>(m_type->currentData().toInt());
        result.baseUrl = m_url->text().trimmed();
        result.username = m_username->text();
        result.password = m_password->text();
        result.token = m_token->text();
        result.useRssGuardProxy = m_proxy->isChecked();
        result.isDefault = m_default->isChecked();
        result.savePath = m_path->text().trimmed();
        result.category = m_category->text().trimmed();
        result.tags = m_tags->text().split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (QString& tag : result.tags) tag = tag.trimmed();
        return result;
      }

      void updateClientFields() {
        const TorrentClientType type = static_cast<TorrentClientType>(m_type->currentData().toInt());
        QString urlExample;
        switch (type) {
          case TorrentClientType::QBittorrent: urlExample = QStringLiteral("http://server.example:8080"); break;
          case TorrentClientType::Transmission:
            urlExample = QStringLiteral("https://server.example/transmission/rpc");
            break;
          case TorrentClientType::Flood: urlExample = QStringLiteral("http://server.example:3000"); break;
          case TorrentClientType::RTorrent:
            urlExample = QStringLiteral("https://server.example/plugins/rpc/rpc.php");
            break;
          case TorrentClientType::Deluge: urlExample = QStringLiteral("http://server.example:8112"); break;
        }
        m_url->setPlaceholderText(urlExample);
        m_url->setToolTip(tr("Expected address for %1. Example: %2")
                            .arg(TorrentClientConfig::typeName(type), urlExample));

        const bool usesFloodToken = type == TorrentClientType::Flood;
        const bool floodTokenEntered = usesFloodToken && !m_token->text().trimmed().isEmpty();
        const bool usesUsername = type != TorrentClientType::Deluge && !floodTokenEntered;
        const bool usesCategory = type == TorrentClientType::QBittorrent || type == TorrentClientType::RTorrent;
        const bool usesTags = type == TorrentClientType::QBittorrent || type == TorrentClientType::Transmission ||
                              type == TorrentClientType::Flood;
        m_username->setEnabled(usesUsername);
        m_password->setEnabled(!floodTokenEntered);
        m_token->setEnabled(usesFloodToken);
        m_category->setEnabled(usesCategory);
        m_tags->setEnabled(usesTags);
        m_form->labelForField(m_username)->setEnabled(usesUsername);
        m_form->labelForField(m_token)->setEnabled(usesFloodToken);
        m_form->labelForField(m_category)->setEnabled(usesCategory);
        m_form->labelForField(m_tags)->setEnabled(usesTags);
        m_username->setPlaceholderText(usesUsername ? tr("Web UI or RPC username") : tr("Not used by Deluge Web"));
        m_password->setPlaceholderText(type == TorrentClientType::Deluge ? tr("Deluge Web password")
                                                                         : tr("Web UI or RPC password"));
      }

    private:
      TorrentClientConfig m_config;
      QLineEdit *m_name, *m_url, *m_username, *m_password, *m_token, *m_path, *m_category, *m_tags;
      QFormLayout* m_form;
      QComboBox* m_type;
      QCheckBox *m_proxy, *m_default;
  };
}

SettingsTorrentClients::SettingsTorrentClients(Settings* settings, QWidget* parent) : SettingsPanel(settings, parent) {}

QString SettingsTorrentClients::title() const { return tr("Torrent clients"); }

QIcon SettingsTorrentClients::icon() const { return qApp->icons()->fromTheme(QStringLiteral("folder-download"), QStringLiteral("go-down")); }

void SettingsTorrentClients::loadUi() {
  auto* outer = new QVBoxLayout(this);
  auto* titleLabel = new QLabel(tr("Torrent clients"), this);
  QFont titleFont = titleLabel->font();
  titleFont.setBold(true);
  titleFont.setPointSize(titleFont.pointSize() + 3);
  titleLabel->setFont(titleFont);
  outer->addWidget(titleLabel);
  m_description = new QLabel(tr("Configure qBittorrent, Transmission, Flood, rTorrent/ruTorrent, or Deluge servers. The Add dialog shows client-specific URL examples and enables only supported options. Torrent-client requests use RSS Guard's network proxy unless disabled per client."), this);
  m_description->setWordWrap(true);
  outer->addWidget(m_description);
  m_showSuccessNotifications = new QCheckBox(tr("Show confirmation after successful torrent sends"), this);
  m_showSuccessNotifications->setToolTip(tr("Show a confirmation dialog after a torrent client accepts a send. Failures are always shown."));
  outer->addWidget(m_showSuccessNotifications);
  m_list = new QListWidget(this);
  outer->addWidget(m_list, 1);
  auto* buttons = new QHBoxLayout();
  auto* add = new QPushButton(tr("Add"), this);
  m_edit = new QPushButton(tr("Edit"), this);
  m_remove = new QPushButton(tr("Remove"), this);
  m_test = new QPushButton(tr("Test connection"), this);
  buttons->addWidget(add);
  buttons->addWidget(m_edit);
  buttons->addWidget(m_remove);
  buttons->addStretch();
  buttons->addWidget(m_test);
  outer->addLayout(buttons);
  connect(add, &QPushButton::clicked, this, &SettingsTorrentClients::addClient);
  connect(m_showSuccessNotifications,
          &QCheckBox::toggled,
          this,
          &SettingsTorrentClients::dirtifySettings);
  connect(m_edit, &QPushButton::clicked, this, &SettingsTorrentClients::editClient);
  connect(m_remove, &QPushButton::clicked, this, &SettingsTorrentClients::removeClient);
  connect(m_test, &QPushButton::clicked, this, &SettingsTorrentClients::testClient);
  connect(m_list, &QListWidget::itemDoubleClicked, this, [this]() { editClient(); });
  connect(m_list, &QListWidget::currentRowChanged, this, &SettingsTorrentClients::updateButtons);
  SettingsPanel::loadUi();
}

void SettingsTorrentClients::loadSettings() {
  onBeginLoadSettings();
  m_clients = TorrentClientConfig::load(settings());
  m_showSuccessNotifications->setChecked(settings()->value(QStringLiteral("TorrentClients"),
                                                            QStringLiteral("showSuccessNotifications"),
                                                            true).toBool());
  refreshList();
  onEndLoadSettings();
}

void SettingsTorrentClients::saveSettings() {
  onBeginSaveSettings();
  TorrentClientConfig::save(settings(), m_clients);
  settings()->setValue(QStringLiteral("TorrentClients"),
                       QStringLiteral("showSuccessNotifications"),
                       m_showSuccessNotifications->isChecked());
  onEndSaveSettings();
}

int SettingsTorrentClients::selectedIndex() const { return m_list == nullptr ? -1 : m_list->currentRow(); }

void SettingsTorrentClients::refreshList(int selected) {
  m_list->clear();
  for (const TorrentClientConfig& client : std::as_const(m_clients)) {
    QString text = QStringLiteral("%1 — %2 — %3").arg(client.name, TorrentClientConfig::typeName(client.type), client.baseUrl);
    if (client.isDefault) text += tr(" (default)");
    auto* item = new QListWidgetItem(text, m_list);
    item->setToolTip(client.useRssGuardProxy ? tr("Uses RSS Guard proxy") : tr("Direct connection; proxy disabled"));
  }
  if (!m_clients.isEmpty()) m_list->setCurrentRow(qBound(0, selected < 0 ? 0 : selected, static_cast<int>(m_clients.size()) - 1));
  updateButtons();
}

void SettingsTorrentClients::addClient() {
  TorrentClientConfig initial;
  initial.useRssGuardProxy = true;
  TorrentClientEditor editor(initial, this);
  if (editor.exec() != QDialog::Accepted) return;
  TorrentClientConfig client = editor.value();
  if (client.isDefault) for (TorrentClientConfig& other : m_clients) other.isDefault = false;
  m_clients.append(client);
  refreshList(m_clients.size() - 1);
  dirtifySettings();
}

void SettingsTorrentClients::editClient() {
  const int row = selectedIndex();
  if (row < 0) return;
  TorrentClientEditor editor(m_clients.at(row), this);
  if (editor.exec() != QDialog::Accepted) return;
  TorrentClientConfig client = editor.value();
  if (client.isDefault) for (int i = 0; i < m_clients.size(); ++i) if (i != row) m_clients[i].isDefault = false;
  m_clients[row] = client;
  refreshList(row);
  dirtifySettings();
}

void SettingsTorrentClients::removeClient() {
  const int row = selectedIndex();
  if (row < 0) return;
  if (QMessageBox::question(this, tr("Remove torrent client"), tr("Remove “%1”?").arg(m_clients.at(row).name)) != QMessageBox::Yes) return;
  m_clients.removeAt(row);
  refreshList(qMin(row, static_cast<int>(m_clients.size()) - 1));
  dirtifySettings();
}

void SettingsTorrentClients::testClient() {
  const int row = selectedIndex();
  if (row < 0) return;
  m_test->setEnabled(false);
  TorrentClient* client = TorrentClient::create(m_clients.at(row), this);
  connect(client, &TorrentClient::testFinished, this, [this, client](bool success, const QString& message) {
    m_test->setEnabled(true);
    if (success) QMessageBox::information(this, tr("Torrent client connection"), message);
    else QMessageBox::warning(this, tr("Torrent client connection"), message);
    client->deleteLater();
  });
  client->testConnection();
}

void SettingsTorrentClients::updateButtons() {
  const bool selected = selectedIndex() >= 0;
  m_edit->setEnabled(selected);
  m_remove->setEnabled(selected);
  m_test->setEnabled(selected);
}
