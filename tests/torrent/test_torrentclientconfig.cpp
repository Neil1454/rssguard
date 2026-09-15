// For license of this file, see <project-root-folder>/LICENSE.md.

#include "torrent/torrentclientconfig.h"

#include <QTest>

class TestTorrentClientConfig : public QObject {
    Q_OBJECT

  private slots:
    void suggestsTransmissionRpcEndpoint();
    void suggestsRuTorrentHttpRpcEndpoint();
    void preservesCustomEndpoint();
    void stripsQBittorrentApiSuffix();
};

void TestTorrentClientConfig::suggestsTransmissionRpcEndpoint() {
  QCOMPARE(TorrentClientConfig::suggestedBaseUrl(TorrentClientType::Transmission,
                                                  QStringLiteral("https://seed.example/transmission/web/")),
           QStringLiteral("https://seed.example/transmission/rpc"));
  QCOMPARE(TorrentClientConfig::suggestedBaseUrl(TorrentClientType::Transmission,
                                                  QStringLiteral("http://192.0.2.1:9091/")),
           QStringLiteral("http://192.0.2.1:9091/transmission/rpc"));
}

void TestTorrentClientConfig::suggestsRuTorrentHttpRpcEndpoint() {
  QCOMPARE(TorrentClientConfig::suggestedBaseUrl(TorrentClientType::RTorrent,
                                                  QStringLiteral("https://seed.example/rutorrent/")),
           QStringLiteral("https://seed.example/rutorrent/plugins/httprpc/action.php"));
}

void TestTorrentClientConfig::preservesCustomEndpoint() {
  QCOMPARE(TorrentClientConfig::suggestedBaseUrl(TorrentClientType::RTorrent,
                                                  QStringLiteral("https://seed.example/plugins/rpc/rpc.php")),
           QStringLiteral("https://seed.example/plugins/rpc/rpc.php"));
  QCOMPARE(TorrentClientConfig::suggestedBaseUrl(TorrentClientType::Flood,
                                                  QStringLiteral("https://seed.example/custom/")),
           QStringLiteral("https://seed.example/custom/"));
}

void TestTorrentClientConfig::stripsQBittorrentApiSuffix() {
  QCOMPARE(TorrentClientConfig::suggestedBaseUrl(TorrentClientType::QBittorrent,
                                                  QStringLiteral("http://192.0.2.2:8080/api/v2/")),
           QStringLiteral("http://192.0.2.2:8080"));
}

QTEST_APPLESS_MAIN(TestTorrentClientConfig)

#include "test_torrentclientconfig.moc"
