// For license of this file, see <project-root-folder>/LICENSE.md.

#include "gui/settings/settingsnetwork.h"

#include "gui/dialogs/filedialog.h"
#include "gui/dialogs/formmain.h"
#include "gui/feedmessageviewer.h"
#include "gui/reusable/networkproxydetails.h"
#include "gui/webbrowser.h"
#include "gui/webviewers/webviewer.h"
#include "miscellaneous/application.h"
#include "miscellaneous/iconfactory.h"
#include "miscellaneous/settings.h"
#include "miscellaneous/settingskeys.h"
#include "network-web/cookiejar.h"
#include "network-web/webfactory.h"

#include <QInputDialog>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QUrl>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QTimer>

SettingsNetwork::SettingsNetwork(Settings* settings, QWidget* parent)
  : SettingsPanel(settings, parent), m_ui(nullptr) {}

void SettingsNetwork::loadUi() {
  m_ui = new Ui::SettingsNetwork();
  m_proxyDetails = new NetworkProxyDetails(this);

  m_ui->setupUi(this);

  m_proxyDetails->setup(false, false);

  m_ui->m_tabBrowserProxy->insertTab(2, m_proxyDetails, tr("Network proxy"));
  auto* testRow = new QWidget(m_proxyDetails);
  auto* testLayout = new QHBoxLayout(testRow);
  testLayout->setContentsMargins(0, 0, 0, 0);
  m_proxyTestResult = new QLabel(tr("Not tested"), testRow);
  m_proxyTestResult->setWordWrap(true);
  m_testProxy = new QPushButton(tr("Test proxy connection"), testRow);
  m_testProxy->setToolTip(tr("Connect to a public IP-check service using exactly the proxy details currently shown. The password is never displayed."));
  testLayout->addWidget(m_proxyTestResult, 1);
  testLayout->addWidget(m_testProxy);
  if (auto* proxyLayout = qobject_cast<QFormLayout*>(m_proxyDetails->layout())) proxyLayout->insertRow(2, testRow);

  connect(m_ui->m_cbFollowHyperlinks, &QCheckBox::STATE_CHANGED, this, &SettingsNetwork::dirtifySettings);
  connect(m_ui->m_cbEnableHttp2, &QCheckBox::STATE_CHANGED, this, &SettingsNetwork::dirtifySettings);
  connect(m_ui->m_cbIgnoreAllCookies, &QCheckBox::STATE_CHANGED, this, &SettingsNetwork::dirtifySettings);
  connect(m_proxyDetails, &NetworkProxyDetails::changed, this, &SettingsNetwork::dirtifySettings);
  connect(m_testProxy, &QPushButton::clicked, this, &SettingsNetwork::testProxyConnection);

  connect(m_ui->m_txtUserAgent, &QLineEdit::textChanged, this, &SettingsNetwork::dirtifySettings);
  connect(m_ui->m_txtUserAgent, &QLineEdit::textChanged, this, &SettingsNetwork::requireRestart);

#if !defined(WEB_ARTICLE_VIEWER_WEBENGINE)
  m_ui->m_tabWebBackends->removeTab(0);
#else
  connect(m_ui->m_txtWebEngineFlags, &QPlainTextEdit::textChanged, this, &SettingsNetwork::dirtifySettings);
  connect(m_ui->m_txtWebEngineFlags, &QPlainTextEdit::textChanged, this, &SettingsNetwork::requireRestart);
#endif

  SettingsPanel::loadUi();
}

void SettingsNetwork::testProxyConnection() {
  const QNetworkProxy proxy = m_proxyDetails->proxy();
  if ((proxy.type() == QNetworkProxy::Socks5Proxy || proxy.type() == QNetworkProxy::HttpProxy) &&
      (proxy.hostName().trimmed().isEmpty() || proxy.port() == 0)) {
    QMessageBox::warning(this, tr("Proxy test"), tr("Enter a proxy host and port before testing."));
    return;
  }

  m_testProxy->setEnabled(false);
  m_proxyTestResult->setText(tr("Testing…"));
  auto* manager = new QNetworkAccessManager(this);
  manager->setProxy(proxy);
  QNetworkRequest request(QUrl(QStringLiteral("https://api.ipify.org")));
  request.setRawHeader("Accept", "text/plain");
  QNetworkReply* reply = manager->get(request);
  auto* timer = new QElapsedTimer();
  timer->start();
  QTimer::singleShot(15000, reply, [reply]() {
    if (reply->isRunning()) reply->abort();
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, manager, timer, proxy]() {
    const qint64 elapsed = timer->elapsed();
    delete timer;
    const QByteArray body = reply->readAll().trimmed();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool success = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300 && !body.isEmpty();
    const QString type = proxy.type() == QNetworkProxy::Socks5Proxy ? QStringLiteral("SOCKS5")
                       : proxy.type() == QNetworkProxy::HttpProxy ? QStringLiteral("HTTP")
                       : proxy.type() == QNetworkProxy::NoProxy ? tr("Direct connection") : tr("System proxy");
    QString result;
    if (success) {
      result = tr("Connected via %1 in %2 ms. Public IP: %3")
                 .arg(type).arg(elapsed).arg(QString::fromUtf8(body).toHtmlEscaped());
      m_proxyTestResult->setText(QStringLiteral("✓ %1").arg(result));
      QMessageBox::information(this, tr("Proxy test successful"), result);
    }
    else {
      result = tr("%1 test failed after %2 ms: %3")
                 .arg(type).arg(elapsed).arg(reply->errorString());
      m_proxyTestResult->setText(QStringLiteral("✗ %1").arg(result));
      QMessageBox::warning(this, tr("Proxy test failed"), result);
    }
    m_testProxy->setEnabled(true);
    reply->deleteLater();
    manager->deleteLater();
  });
}

SettingsNetwork::~SettingsNetwork() {
  if (m_ui != nullptr) {
    delete m_ui;
  }
}

QIcon SettingsNetwork::icon() const {
  return qApp->icons()->fromTheme(QSL("applications-network"), QSL("internet-services"));
}

void SettingsNetwork::loadSettings() {
  onBeginLoadSettings();

  m_ui->m_cbFollowHyperlinks->setChecked(settings()->value(GROUP(Web), SETTING(Web::FollowLinks)).toBool());
  m_ui->m_cbEnableHttp2->setChecked(settings()->value(GROUP(Network), SETTING(Network::EnableHttp2)).toBool());
  m_ui->m_cbIgnoreAllCookies->setChecked(settings()->value(GROUP(Network), SETTING(Network::IgnoreAllCookies)).toBool());
  m_ui->m_txtUserAgent->setText(settings()->value(GROUP(Network), SETTING(Network::CustomUserAgent)).toString());

  // Load the settings.
  QNetworkProxy::ProxyType selected_proxy_type =
    static_cast<QNetworkProxy::ProxyType>(settings()->value(GROUP(Proxy), SETTING(Proxy::Type)).toInt());

  m_proxyDetails->setProxy(QNetworkProxy(selected_proxy_type,
                                         settings()->value(GROUP(Proxy), SETTING(Proxy::Host)).toString(),
                                         settings()->value(GROUP(Proxy), SETTING(Proxy::Port)).toInt(),
                                         settings()->value(GROUP(Proxy), SETTING(Proxy::Username)).toString(),
                                         settings()->password(GROUP(Proxy), SETTING(Proxy::Password)).toString()));

#if defined(WEB_ARTICLE_VIEWER_WEBENGINE)
  m_ui->m_txtWebEngineFlags
    ->setPlainText(settings()->value(GROUP(Web), SETTING(Web::WebEngineChromiumFlags)).toString());
#endif

  onEndLoadSettings();
}

void SettingsNetwork::saveSettings() {
  onBeginSaveSettings();

  settings()->setValue(GROUP(Web), Web::FollowLinks, m_ui->m_cbFollowHyperlinks->isChecked());
  settings()->setValue(GROUP(Network), Network::EnableHttp2, m_ui->m_cbEnableHttp2->isChecked());
  settings()->setValue(GROUP(Network), Network::IgnoreAllCookies, m_ui->m_cbIgnoreAllCookies->isChecked());
  settings()->setValue(GROUP(Network), Network::CustomUserAgent, m_ui->m_txtUserAgent->text());

  auto proxy = m_proxyDetails->proxy();

  settings()->setValue(GROUP(Proxy), Proxy::Type, int(proxy.type()));
  settings()->setValue(GROUP(Proxy), Proxy::Host, proxy.hostName());
  settings()->setValue(GROUP(Proxy), Proxy::Username, proxy.user());
  settings()->setPassword(GROUP(Proxy), Proxy::Password, proxy.password());
  settings()->setValue(GROUP(Proxy), Proxy::Port, proxy.port());

  qApp->web()->updateProxy();
  qApp->web()->cookieJar()->updateSettings();
  qApp->mainForm()->tabWidget()->feedMessageViewer()->webBrowser()->viewer()->reloadNetworkSettings();

#if defined(WEB_ARTICLE_VIEWER_WEBENGINE)
  settings()->setValue(GROUP(Web), Web::WebEngineChromiumFlags, m_ui->m_txtWebEngineFlags->toPlainText());
#endif

  onEndSaveSettings();
}
