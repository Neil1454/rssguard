// For license of this file, see <project-root-folder>/LICENSE.md.

#include "core/message.h"
#include "torrent/torrentextractor.h"

#include <QTest>

class TestTorrentExtractor : public QObject {
    Q_OBJECT

  private slots:
    void extractsSupportedSources();
    void doesNotTreatArticleAsTorrent();
    void deduplicatesBulkSelection();
};

void TestTorrentExtractor::extractsSupportedSources() {
  Message message;
  message.m_url = QStringLiteral("https://example.test/article");
  message.m_enclosures.append(QSharedPointer<MessageEnclosure>::create(
    QStringLiteral("https://example.test/download?id=12"), QStringLiteral("application/x-bittorrent")));
  message.m_contents = QStringLiteral("<a href=\"magnet:?xt=urn:btih:ABC123&amp;dn=Example\">Download</a>");
  message.m_rawContents = QStringLiteral("https://files.example.test/release.torrent?key=123");

  const QStringList urls = TorrentExtractor::extract(message);
  QCOMPARE(urls.size(), 3);
  QVERIFY(urls.contains(QStringLiteral("https://example.test/download?id=12")));
  QVERIFY(urls.contains(QStringLiteral("magnet:?xt=urn:btih:ABC123&dn=Example")));
  QVERIFY(urls.contains(QStringLiteral("https://files.example.test/release.torrent?key=123")));
}

void TestTorrentExtractor::doesNotTreatArticleAsTorrent() {
  Message message;
  message.m_url = QStringLiteral("https://example.test/article/42");
  message.m_contents = QStringLiteral("Normal article text");
  QVERIFY(TorrentExtractor::extract(message).isEmpty());
}

void TestTorrentExtractor::deduplicatesBulkSelection() {
  Message first;
  Message second;
  first.m_enclosures.append(QSharedPointer<MessageEnclosure>::create(
    QStringLiteral("magnet:?xt=urn:btih:SAME"), QStringLiteral("application/x-bittorrent")));
  second.m_contents = QStringLiteral("<a href='MAGNET:?xt=urn:btih:SAME'>same</a>");

  const TorrentExtractionResult result = TorrentExtractor::extract({first, second});
  QCOMPARE(result.urls.size(), 1);
  QCOMPARE(result.duplicatesRemoved, 1);
  QCOMPARE(result.messagesWithoutTorrent, 0);
}

QTEST_APPLESS_MAIN(TestTorrentExtractor)

#include "test_torrentextractor.moc"
