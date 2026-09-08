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
        auto* form = new QFormLayout();
        m_name = new QLineEdit(initial.name, this);
        m_type = new QComboBox(this);
        for (int i = 0; i < 4; ++i) m_type->addItem(TorrentClientConfig::typeName(static_cast<TorrentClientType>(i)), i);
        m_type->setCurrentIndex(static_cast<int>(initial.type));
        m_url = new QLineEdit(initial.baseUrl, this);
        m_url->setPlaceholderText(QStringLiteral("https://server.example/rpc"));
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
        form->addRow(tr("Name:"), m_name);
        form->addRow(tr("Client type:"), m_type);
        form->addRow(tr("Server/base URL:"), m_url);
        form->addRow(tr("Username:"), m_username);
        form->addRow(tr("Password:"), m_password);
        form->addRow(tr("Flood token (optional):"), m_token);
        form->addRow(QString(), m_proxy);
        form->addRow(QString(), m_default);
        form->addRow(tr("Default save path (optional):"), m_path);
        form->addRow(tr("Category/label (optional):"), m_category);
        form->addRow(tr("Tags, comma-separated (optional):"), m_tags);
        outer->addLayout(form);
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

    private:
      TorrentClientConfig m_config;
      QLineEdit *m_name, *m_url, *m_username, *m_password, *m_token, *m_path, *m_category, *m_tags;
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
  m_description = new QLabel(tr("Configure one or more qBittorrent, Transmission, Flood, or rTorrent servers. Torrent-client requests use RSS Guard's network proxy unless disabled per client."), this);
  m_description->setWordWrap(true);
  outer->addWidget(m_description);
  m_list = new QListWidget(this);
  m_list->setAlternatingRowColors(true);
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
  refreshList();
  onEndLoadSettings();
}

void SettingsTorrentClients::saveSettings() {
  onBeginSaveSettings();
  TorrentClientConfig::save(settings(), m_clients);
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
