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
#include <QHostAddress>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QUrl>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSharedPointer>
#include <QTimer>
#include <QVBoxLayout>

SettingsNetwork::SettingsNetwork(Settings* settings, QWidget* parent)
  : SettingsPanel(settings, parent), m_ui(nullptr) {}

void SettingsNetwork::loadUi() {
  m_ui = new Ui::SettingsNetwork();
  m_proxyDetails = new NetworkProxyDetails(this);

  m_ui->setupUi(this);

  m_proxyDetails->setup(false, false);

  m_ui->m_tabBrowserProxy->insertTab(2, m_proxyDetails, tr("Network proxy"));
  auto* testRow = new QWidget(m_proxyDetails);
  auto* testLayout = new QVBoxLayout(testRow);
  testLayout->setContentsMargins(0, 0, 0, 0);
  auto* testControls = new QHBoxLayout();
  m_proxyTestResult = new QLabel(tr("Not tested"), testRow);
  m_proxyTestResult->setWordWrap(true);
  m_testProxy = new QPushButton(tr("Run proxy privacy check"), testRow);
  m_testProxy->setToolTip(tr("Makes one request through the shown proxy and one deliberate direct control request, then compares their public IP addresses. No torrent data or credentials are sent."));
  testControls->addWidget(m_proxyTestResult, 1);
  testControls->addWidget(m_testProxy);
  testLayout->addLayout(testControls);
  auto* privacyScope = new QLabel(
    tr("<b>What this can prove:</b> whether this RSS Guard network request reached the test service through a different public IP. "
       "<b>What it cannot prove:</b> freedom from operating-system DNS leaks, browser/WebEngine leaks, or torrent peer-traffic leaks. "
       "A torrent client's downloads and uploads use that client's own proxy/VPN settings, not this RSS Guard setting."), testRow);
  privacyScope->setWordWrap(true);
  privacyScope->setTextFormat(Qt::RichText);
  testLayout->addWidget(privacyScope);
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

  const QString testDisclosure = tr(
    "This privacy check contacts api.ipify.org twice: once through the proxy shown here and once using an intentional direct connection. "
    "The service will see each request's public IP and ordinary HTTPS connection metadata. The direct control request is necessary to detect whether the proxy changes the visible address.\n\n"
    "Continue?");
  if (QMessageBox::question(this, tr("Run proxy privacy check?"), testDisclosure,
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;

  m_testProxy->setEnabled(false);
  m_proxyTestResult->setText(tr("Testing proxied and direct paths…"));

  struct PathResult {
    bool success = false;
    QString address;
    QString error;
    qint64 elapsed = 0;
  };
  struct TestState {
    PathResult proxied;
    PathResult direct;
    int completed = 0;
  };
  auto state = QSharedPointer<TestState>::create();
  auto* proxyManager = new QNetworkAccessManager(this);
  auto* directManager = new QNetworkAccessManager(this);
  proxyManager->setProxy(proxy);
  directManager->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));

  const QString type = proxy.type() == QNetworkProxy::Socks5Proxy ? QStringLiteral("SOCKS5")
                     : proxy.type() == QNetworkProxy::HttpProxy ? QStringLiteral("HTTP")
                     : proxy.type() == QNetworkProxy::NoProxy ? tr("Direct connection") : tr("System proxy");
  const auto startRequest = [this, state, proxyManager, directManager, type]
                            (QNetworkAccessManager* manager, bool proxied) {
    QNetworkRequest request(QUrl(QStringLiteral("https://api.ipify.org")));
    request.setRawHeader("Accept", "text/plain");
    QNetworkReply* reply = manager->get(request);
    auto* timer = new QElapsedTimer();
    timer->start();
    QTimer::singleShot(15000, reply, [reply]() {
      if (reply->isRunning()) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, state, proxyManager, directManager, type, reply, timer, proxied]() {
      PathResult& result = proxied ? state->proxied : state->direct;
      result.elapsed = timer->elapsed();
      delete timer;
      const QByteArray body = reply->readAll().trimmed();
      QHostAddress parsedAddress;
      const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      result.success = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300 &&
                       parsedAddress.setAddress(QString::fromUtf8(body));
      if (result.success) result.address = parsedAddress.toString();
      else result.error = reply->error() == QNetworkReply::NoError
                            ? tr("The IP-check service returned an invalid response.") : reply->errorString();
      reply->deleteLater();
      if (++state->completed < 2) return;

      QString heading;
      QString detail;
      QMessageBox::Icon icon = QMessageBox::Warning;
      if (!state->proxied.success) {
        heading = tr("Proxy path failed");
        detail = tr("The %1 request did not reach the public test service: %2\n\nThe proxy cannot be confirmed from this test.")
                   .arg(type, state->proxied.error);
        icon = QMessageBox::Critical;
      }
      else if (!state->direct.success) {
        heading = tr("Proxy works; comparison incomplete");
        detail = tr("Proxied public IP: %1 (%2 ms)\nDirect control request failed: %3\n\nThe proxy path works, but its address could not be compared with the normal public address.")
                   .arg(state->proxied.address).arg(state->proxied.elapsed).arg(state->direct.error);
      }
      else if (state->proxied.address != state->direct.address) {
        heading = tr("Public-IP check passed");
        detail = tr("Proxied public IP: %1 (%2 ms)\nDirect public IP: %3 (%4 ms)\n\nThe test service saw a different address through %5. This confirms this RSS Guard request used a different public route.")
                   .arg(state->proxied.address).arg(state->proxied.elapsed)
                   .arg(state->direct.address).arg(state->direct.elapsed).arg(type);
        icon = QMessageBox::Information;
      }
      else {
        heading = tr("Public IP did not change");
        detail = tr("Both paths showed %1. The proxy may be local/transparent, may exit through the same public address, or may not be providing the privacy expected.")
                   .arg(state->proxied.address);
      }

      detail += tr("\n\nNot tested: operating-system DNS queries, WebEngine/WebRTC behaviour, external links opened by another browser, or torrent peer traffic inside your torrent client. HTTPS hides page content from the proxy, but the proxy operator can still know your account/address, connection times and destination hosts. Configure and test the torrent client's own proxy or VPN separately.");
      m_proxyTestResult->setText(QStringLiteral("%1 %2 — %3")
        .arg(icon == QMessageBox::Information ? QStringLiteral("✓") : QStringLiteral("⚠"), heading,
             state->proxied.success ? tr("proxy IP %1").arg(state->proxied.address) : state->proxied.error));
      QMessageBox message(icon, heading, detail, QMessageBox::Ok, this);
      message.exec();
      m_testProxy->setEnabled(true);
      proxyManager->deleteLater();
      directManager->deleteLater();
    });
  };
  startRequest(proxyManager, true);
  startRequest(directManager, false);
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
