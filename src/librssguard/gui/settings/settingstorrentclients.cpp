// For license of this file, see <project-root-folder>/LICENSE.md.

#include "gui/settings/settingstorrentclients.h"

#include "miscellaneous/application.h"
#include "miscellaneous/iconfactory.h"
#include "miscellaneous/settings.h"
#include "torrent/torrentclient.h"

#include <QCheckBox>
#include <QColor>
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
#include <QPixmap>
#include <QSpinBox>
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
        for (int i = 0; i < 7; ++i) m_type->addItem(TorrentClientConfig::typeName(static_cast<TorrentClientType>(i)), i);
        m_type->setCurrentIndex(static_cast<int>(initial.type));
        m_url = new QLineEdit(initial.baseUrl, this);
        m_username = new QLineEdit(initial.username, this);
        m_password = new QLineEdit(initial.password, this);
        m_password->setEchoMode(QLineEdit::Password);
        m_token = new QLineEdit(initial.token, this);
        m_token->setEchoMode(QLineEdit::Password);
        m_enabled = new QCheckBox(tr("Enabled"), this);
        m_enabled->setChecked(initial.enabled);
        m_priority = new QSpinBox(this);
        m_priority->setRange(1, 999);
        m_priority->setValue(qMax(1, initial.priority));
        m_priority->setToolTip(tr("Position of this client in notification buttons and menus. Priority 1 appears first."));
        m_color = new QComboBox(this);
        const QList<QPair<QString, QString>> colors{
          {tr("Default system colour"), QString()}, {tr("Blue"), QStringLiteral("#1976d2")},
          {tr("Green"), QStringLiteral("#2e7d32")}, {tr("Red"), QStringLiteral("#c62828")},
          {tr("Orange"), QStringLiteral("#ef6c00")}, {tr("Purple"), QStringLiteral("#6a1b9a")},
          {tr("Teal"), QStringLiteral("#00796b")}, {tr("Grey"), QStringLiteral("#616161")},
          {tr("Yellow"), QStringLiteral("#f9a825")}, {tr("Pink"), QStringLiteral("#ad1457")}
        };
        for (const auto& color : colors) {
          if (color.second.isEmpty()) m_color->addItem(color.first, color.second);
          else {
            QPixmap swatch(18, 18);
            swatch.fill(QColor(color.second));
            m_color->addItem(QIcon(swatch), color.first, color.second);
          }
        }
        const int selectedColor = m_color->findData(initial.buttonColor);
        m_color->setCurrentIndex(selectedColor >= 0 ? selectedColor : 0);
        m_color->setToolTip(tr("Colour used for this client's button on new-article notifications."));
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
        m_form->addRow(tr("API token (optional):"), m_token);
        m_form->addRow(QString(), m_enabled);
        m_form->addRow(tr("Button priority:"), m_priority);
        m_form->addRow(tr("Notification button colour:"), m_color);
        m_form->addRow(QString(), m_proxy);
        m_form->addRow(QString(), m_default);
        m_form->addRow(tr("Default save path (optional):"), m_path);
        m_form->addRow(tr("Category/label (optional):"), m_category);
        m_form->addRow(tr("Tags, comma-separated (optional):"), m_tags);
        m_url->setToolTip(tr("Enter the web or RPC address for the selected client. The example changes with Client type."));
        m_username->setToolTip(tr("Login username. Disabled when the selected client does not use one."));
        m_password->setToolTip(tr("Password used by the client's Web UI or RPC service."));
        m_token->setToolTip(tr("Authentication token used by clients that support token login."));
        m_path->setToolTip(tr("A directory on the torrent server, not necessarily a folder on this computer."));
        m_category->setToolTip(tr("qBittorrent category, or rTorrent custom label. Disabled for clients that do not support it here."));
        m_tags->setToolTip(tr("Comma-separated qBittorrent/Flood tags or Transmission labels. Disabled for unsupported clients."));
        m_path->setPlaceholderText(tr("Example: /downloads/rss"));
        m_category->setPlaceholderText(tr("Example: tv"));
        m_tags->setPlaceholderText(tr("Example: rss, automatic"));
        connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { updateClientFields(); });
        connect(m_token, &QLineEdit::textChanged, this, [this]() { updateClientFields(); });
        connect(m_enabled, &QCheckBox::toggled, this, [this](bool enabled) {
          m_priority->setEnabled(enabled);
          m_form->labelForField(m_priority)->setEnabled(enabled);
        });
        m_priority->setEnabled(initial.enabled);
        m_form->labelForField(m_priority)->setEnabled(initial.enabled);
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
        result.buttonColor = m_color->currentData().toString();
        result.enabled = m_enabled->isChecked();
        result.priority = m_priority->value();
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
            urlExample = QStringLiteral("https://server.example/plugins/httprpc/action.php");
            break;
          case TorrentClientType::Deluge: urlExample = QStringLiteral("http://server.example:8112"); break;
          case TorrentClientType::RQBit: urlExample = QStringLiteral("http://server.example:3030"); break;
          case TorrentClientType::Porla: urlExample = QStringLiteral("http://server.example:1337"); break;
        }
        m_url->setPlaceholderText(urlExample);
        m_url->setToolTip(tr("Expected address for %1. Example: %2")
                            .arg(TorrentClientConfig::typeName(type), urlExample));

        const bool usesToken = type == TorrentClientType::Flood || type == TorrentClientType::Porla;
        const bool tokenEntered = usesToken && !m_token->text().trimmed().isEmpty();
        const bool usesUsername = type != TorrentClientType::Deluge && type != TorrentClientType::Porla && !tokenEntered;
        const bool usesPassword = type != TorrentClientType::Porla && !tokenEntered;
        const bool usesCategory = type == TorrentClientType::QBittorrent || type == TorrentClientType::RTorrent ||
                                  type == TorrentClientType::Porla;
        const bool usesTags = type == TorrentClientType::QBittorrent || type == TorrentClientType::Transmission ||
                              type == TorrentClientType::Flood;
        m_username->setEnabled(usesUsername);
        m_password->setEnabled(usesPassword);
        m_token->setEnabled(usesToken);
        m_category->setEnabled(usesCategory);
        m_tags->setEnabled(usesTags);
        m_form->labelForField(m_username)->setEnabled(usesUsername);
        m_form->labelForField(m_password)->setEnabled(usesPassword);
        m_form->labelForField(m_token)->setEnabled(usesToken);
        m_form->labelForField(m_category)->setEnabled(usesCategory);
        m_form->labelForField(m_tags)->setEnabled(usesTags);
        m_username->setPlaceholderText(usesUsername ? tr("Web UI or RPC username") : tr("Not used by this client"));
        m_password->setPlaceholderText(type == TorrentClientType::Deluge ? tr("Deluge Web password")
                                                                         : (usesPassword ? tr("Web UI or RPC password")
                                                                                         : tr("Not used by this client")));
        m_token->setPlaceholderText(type == TorrentClientType::Porla ? tr("Required JWT from: porla auth:token")
                                                                     : tr("Existing Flood JWT token"));
        m_token->setToolTip(type == TorrentClientType::Porla
                              ? tr("Required Porla bearer JWT generated on the Porla server with 'porla auth:token'.")
                              : tr("Flood only: an existing JWT token can be used instead of username and password."));
        m_category->setPlaceholderText(type == TorrentClientType::Porla ? tr("Example preset: tv") : tr("Example: tv"));
        m_category->setToolTip(type == TorrentClientType::Porla
                                ? tr("Porla preset name. The preset must already exist on the Porla server.")
                                : tr("qBittorrent category, or rTorrent custom label. Disabled for clients that do not support it here."));
      }

    private:
      TorrentClientConfig m_config;
      QLineEdit *m_name, *m_url, *m_username, *m_password, *m_token, *m_path, *m_category, *m_tags;
      QFormLayout* m_form;
      QComboBox *m_type, *m_color;
      QCheckBox *m_enabled, *m_proxy, *m_default;
      QSpinBox* m_priority;
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
  m_description = new QLabel(tr("Configure qBittorrent, Transmission, Flood, rTorrent/ruTorrent, Deluge, rQBit, or Porla servers. The Add dialog shows client-specific URL examples and enables only supported options. Torrent-client requests use RSS Guard's network proxy unless disabled per client."), this);
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
  m_testAll = new QPushButton(tr("Test all enabled"), this);
  buttons->addWidget(add);
  buttons->addWidget(m_edit);
  buttons->addWidget(m_remove);
  buttons->addStretch();
  buttons->addWidget(m_test);
  buttons->addWidget(m_testAll);
  outer->addLayout(buttons);
  connect(add, &QPushButton::clicked, this, &SettingsTorrentClients::addClient);
  connect(m_showSuccessNotifications,
          &QCheckBox::toggled,
          this,
          &SettingsTorrentClients::dirtifySettings);
  connect(m_edit, &QPushButton::clicked, this, &SettingsTorrentClients::editClient);
  connect(m_remove, &QPushButton::clicked, this, &SettingsTorrentClients::removeClient);
  connect(m_test, &QPushButton::clicked, this, &SettingsTorrentClients::testClient);
  connect(m_testAll, &QPushButton::clicked, this, &SettingsTorrentClients::testAllClients);
  connect(m_list, &QListWidget::itemDoubleClicked, this, [this]() { editClient(); });
  connect(m_list, &QListWidget::currentRowChanged, this, &SettingsTorrentClients::updateButtons);
  SettingsPanel::loadUi();
}

void SettingsTorrentClients::loadSettings() {
  onBeginLoadSettings();
  m_clients = TorrentClientConfig::load(settings());
  normalizeClientOrder();
  m_showSuccessNotifications->setChecked(settings()->value(QStringLiteral("TorrentClients"),
                                                            QStringLiteral("showSuccessNotifications"),
                                                            true).toBool());
  refreshList();
  onEndLoadSettings();
}

void SettingsTorrentClients::saveSettings() {
  onBeginSaveSettings();
  normalizeClientOrder();
  TorrentClientConfig::save(settings(), m_clients);
  settings()->setValue(QStringLiteral("TorrentClients"),
                       QStringLiteral("showSuccessNotifications"),
                       m_showSuccessNotifications->isChecked());
  onEndSaveSettings();
}

int SettingsTorrentClients::selectedIndex() const { return m_list == nullptr ? -1 : m_list->currentRow(); }

void SettingsTorrentClients::normalizeClientOrder() {
  const QList<TorrentClientConfig> enabled = TorrentClientConfig::enabledInPriorityOrder(m_clients);
  QList<TorrentClientConfig> normalized = enabled;
  for (const TorrentClientConfig& client : std::as_const(m_clients)) if (!client.enabled) normalized.append(client);
  int priority = 1;
  for (TorrentClientConfig& client : normalized) client.priority = client.enabled ? priority++ : 0;
  m_clients = normalized;
}

void SettingsTorrentClients::refreshList(int selected) {
  m_list->clear();
  for (const TorrentClientConfig& client : std::as_const(m_clients)) {
    QString text = QStringLiteral("%1 — %2 — %3").arg(client.name, TorrentClientConfig::typeName(client.type), client.baseUrl);
    if (client.enabled) text = QStringLiteral("%1. %2").arg(client.priority).arg(text);
    else text += tr(" (disabled)");
    if (client.isDefault) text += tr(" (default)");
    auto* item = new QListWidgetItem(text, m_list);
    if (!client.enabled) item->setForeground(palette().color(QPalette::ColorGroup::Disabled, QPalette::ColorRole::Text));
    if (!client.buttonColor.isEmpty()) {
      QPixmap swatch(14, 14);
      swatch.fill(QColor(client.buttonColor));
      item->setIcon(QIcon(swatch));
    }
    item->setToolTip(client.useRssGuardProxy ? tr("Uses RSS Guard proxy") : tr("Direct connection; proxy disabled"));
  }
  if (!m_clients.isEmpty()) m_list->setCurrentRow(qBound(0, selected < 0 ? 0 : selected, static_cast<int>(m_clients.size()) - 1));
  updateButtons();
}

void SettingsTorrentClients::addClient() {
  TorrentClientConfig initial;
  initial.useRssGuardProxy = true;
  initial.priority = TorrentClientConfig::enabledInPriorityOrder(m_clients).size() + 1;
  TorrentClientEditor editor(initial, this);
  if (editor.exec() != QDialog::Accepted) return;
  TorrentClientConfig client = editor.value();
  if (client.isDefault) for (TorrentClientConfig& other : m_clients) other.isDefault = false;
  const int target = client.enabled
                       ? qBound(0, client.priority - 1, TorrentClientConfig::enabledInPriorityOrder(m_clients).size())
                       : m_clients.size();
  m_clients.insert(target, client);
  normalizeClientOrder();
  refreshList(target);
  dirtifySettings();
}

void SettingsTorrentClients::editClient() {
  const int row = selectedIndex();
  if (row < 0) return;
  TorrentClientEditor editor(m_clients.at(row), this);
  if (editor.exec() != QDialog::Accepted) return;
  TorrentClientConfig client = editor.value();
  if (client.isDefault) for (int i = 0; i < m_clients.size(); ++i) if (i != row) m_clients[i].isDefault = false;
  m_clients.removeAt(row);
  normalizeClientOrder();
  const int target = client.enabled
                       ? qBound(0, client.priority - 1, TorrentClientConfig::enabledInPriorityOrder(m_clients).size())
                       : m_clients.size();
  m_clients.insert(target, client);
  normalizeClientOrder();
  refreshList(target);
  dirtifySettings();
}

void SettingsTorrentClients::removeClient() {
  const int row = selectedIndex();
  if (row < 0) return;
  if (QMessageBox::question(this, tr("Remove torrent client"), tr("Remove “%1”?").arg(m_clients.at(row).name)) != QMessageBox::Yes) return;
  m_clients.removeAt(row);
  normalizeClientOrder();
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

void SettingsTorrentClients::testAllClients() {
  m_testAllQueue.clear();
  m_testAllResults.clear();
  for (const TorrentClientConfig& client : std::as_const(m_clients)) if (client.enabled) m_testAllQueue.append(client);
  if (m_testAllQueue.isEmpty()) {
    QMessageBox::information(this, tr("Test torrent clients"), tr("There are no enabled torrent clients to test."));
    return;
  }
  m_test->setEnabled(false);
  m_testAll->setEnabled(false);
  testNextClient();
}

void SettingsTorrentClients::testNextClient() {
  if (m_testAllQueue.isEmpty()) {
    QMessageBox result(this);
    result.setWindowTitle(tr("Torrent client test results"));
    result.setIcon(m_testAllFailures == 0 ? QMessageBox::Information : QMessageBox::Warning);
    result.setText(tr("Tested %1 enabled client(s): %2 passed, %3 failed.")
                     .arg(m_testAllResults.size()).arg(m_testAllResults.size() - m_testAllFailures).arg(m_testAllFailures));
    result.setInformativeText(m_testAllResults.join(QStringLiteral("<br>")));
    result.exec();
    m_testAllFailures = 0;
    m_testAll->setEnabled(true);
    updateButtons();
    return;
  }
  const TorrentClientConfig config = m_testAllQueue.takeFirst();
  TorrentClient* client = TorrentClient::create(config, this);
  connect(client, &TorrentClient::testFinished, this, [this, client, config](bool success, const QString& message) {
    if (!success) ++m_testAllFailures;
    m_testAllResults.append(QStringLiteral("%1 <b>%2</b> — %3")
                              .arg(success ? QStringLiteral("&#10004;") : QStringLiteral("&#10008;"),
                                   config.name.toHtmlEscaped(), message.toHtmlEscaped()));
    client->deleteLater();
    testNextClient();
  });
  client->testConnection();
}

void SettingsTorrentClients::updateButtons() {
  const bool selected = selectedIndex() >= 0;
  m_edit->setEnabled(selected);
  m_remove->setEnabled(selected);
  m_test->setEnabled(selected);
  if (m_testAll != nullptr && m_testAllQueue.isEmpty()) m_testAll->setEnabled(true);
}
