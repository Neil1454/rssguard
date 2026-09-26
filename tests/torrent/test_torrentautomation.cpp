// For license of this file, see <project-root-folder>/LICENSE.md.

#include "torrent/torrentautomationconfig.h"
#include "torrent/torrentautomationengine.h"

#include <QTest>

class TestTorrentAutomation : public QObject {
    Q_OBJECT

  private slots:
    void exclusiveModeRequiresEnabledAndArmed();
    void balancedRoutingForcesAnEligibleAlternative();
    void balancedRoutingKeepsOnlyAvailableClient();
    void rtorrentLoadCommandsSetAddedTimeAndResume();
    void startupTorrentBaselineRejectsOldAndUndatedItems();
};

void TestTorrentAutomation::exclusiveModeRequiresEnabledAndArmed() {
  TorrentAutomationConfig config;
  QVERIFY(!config.exclusiveModeActive());
  QCOMPARE(config.exclusiveStrategy, TorrentRoutingStrategy::RoundRobin);
  QCOMPARE(config.exclusiveMaximumConsecutiveAssignments, 1);
  QCOMPARE(config.exclusiveRequestTimeoutSeconds, 2);
  QCOMPARE(config.exclusiveRapidRetryAttempts, 3);
  QCOMPARE(config.exclusiveRapidRetryDelayMs, 0);
  config.exclusiveModeEnabled = true;
  QVERIFY(!config.exclusiveModeActive());
  config.exclusiveModeArmed = true;
  QVERIFY(config.exclusiveModeActive());
  config.exclusiveModeEnabled = false;
  QVERIFY(!config.exclusiveModeActive());
}

void TestTorrentAutomation::balancedRoutingForcesAnEligibleAlternative() {
  TorrentClientConfig appBox;
  appBox.id = QStringLiteral("app-box");
  TorrentClientConfig rapidRu;
  rapidRu.id = QStringLiteral("rapid-ru");
  const QList<TorrentClientConfig> clients{appBox, rapidRu};

  QCOMPARE(TorrentAutomationEngine::fairBalancedCandidates({0, 1}, clients,
                                                            QStringLiteral("app-box"), 1, 1),
           QList<int>{1});
  QCOMPARE(TorrentAutomationEngine::fairBalancedCandidates({0, 1}, clients,
                                                            QStringLiteral("app-box"), 1, 2),
           (QList<int>{0, 1}));
}

void TestTorrentAutomation::balancedRoutingKeepsOnlyAvailableClient() {
  TorrentClientConfig appBox;
  appBox.id = QStringLiteral("app-box");
  TorrentClientConfig rapidRu;
  rapidRu.id = QStringLiteral("rapid-ru");
  const QList<TorrentClientConfig> clients{appBox, rapidRu};

  QCOMPARE(TorrentAutomationEngine::fairBalancedCandidates({0}, clients,
                                                            QStringLiteral("app-box"), 8, 1),
           QList<int>{0});
}

void TestTorrentAutomation::rtorrentLoadCommandsSetAddedTimeAndResume() {
  QCOMPARE(RTorrentClient::startCompatibilityCommands(1234567890),
           (QStringList{QStringLiteral("d.custom.set=addtime,1234567890"),
                        QStringLiteral("d.start=")}));
}

void TestTorrentAutomation::startupTorrentBaselineRejectsOldAndUndatedItems() {
  const QDateTime launch = QDateTime::fromString(QStringLiteral("2026-09-26T06:00:00Z"), Qt::ISODate);
  const QDateTime now = launch.addSecs(60);
  QVERIFY(TorrentAutomationEngine::isOldStartupTorrent(true, true, true,
                                                        launch.addSecs(-60), launch, now));
  QVERIFY(TorrentAutomationEngine::isOldStartupTorrent(true, true, false, {}, launch, now));
  QVERIFY(!TorrentAutomationEngine::isOldStartupTorrent(true, true, true,
                                                         launch.addSecs(20), launch, now));
  QVERIFY(!TorrentAutomationEngine::isOldStartupTorrent(false, true, false, {}, launch, now));
  QVERIFY(!TorrentAutomationEngine::isOldStartupTorrent(true, false, false, {}, launch, now));
}

QTEST_APPLESS_MAIN(TestTorrentAutomation)

#include "test_torrentautomation.moc"
