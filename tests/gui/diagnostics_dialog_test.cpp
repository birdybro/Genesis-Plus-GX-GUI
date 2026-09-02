#include "genplusgx/ui/diagnostics_dialog.h"
#include "genplusgx/ui/main_window.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTest>

namespace {

class DiagnosticsDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void reportIsPrivateInspectableCopyableAndRefreshable();
};

void DiagnosticsDialogTest::reportIsPrivateInspectableCopyableAndRefreshable()
{
  genplusgx::ui::MainWindow window;
  int generation = 1;
  window.setDiagnosticsSnapshotProvider([&generation] {
    auto snapshot = genplusgx::diagnostics::staticDiagnosticsSnapshot();
    snapshot.renderer = "Test renderer generation " + std::to_string(generation);
    snapshot.audioDevice = "Synthetic audio";
    snapshot.loadedGame = "/users/private/Game.bin";
    snapshot.loadedSystem = "Genesis";
    snapshot.loadedRegion = "USA token=do-not-copy";
    snapshot.controllerCount = 2U;
    snapshot.audioBufferedFrames = 128U;
    snapshot.audioCapacityFrames = 512U;
    snapshot.rewindEnabled = true;
    snapshot.rewindSnapshots = 7U;
    snapshot.rewindPayloadBytes = 7U * 1024U;
    snapshot.rewindMemoryLimitBytes = 64U * 1024U;
    snapshot.runAheadEnabled = true;
    snapshot.runAheadSupported = true;
    snapshot.runAheadActive = true;
    snapshot.runAheadVerified = true;
    snapshot.runAheadFrames = 2U;
    snapshot.runAheadSpeculativeFrames = 84U;
    snapshot.runAheadRollbacks = 42U;
    snapshot.runAheadStateBytes = 192U * 1024U;
    snapshot.runAheadStateCapacityBytes = 256U * 1024U;
    snapshot.netplayState = "Authenticated peer connected";
    snapshot.netplaySentPackets = 180U;
    snapshot.netplayReceivedPackets = 179U;
    snapshot.netplayQueuedFrames = 2U;
    snapshot.netplayQueueCapacity = 256U;
    snapshot.netplayPredictedFrames = 12U;
    snapshot.netplayRollbackRequests = 2U;
    snapshot.netplayRollbacks = 2U;
    snapshot.netplayHistoryFrames = 9U;
    snapshot.netplayHistoryBytes = 196'608U;
    snapshot.achievementsState = "Authenticated; recognized game active";
    snapshot.achievementsHardcore = true;
    snapshot.achievementRequestQueueDepth = 1U;
    snapshot.achievementResponseQueueDepth = 2U;
    snapshot.achievementQueueCapacity = 32U;
    snapshot.completedAchievementRequests = 8U;
    snapshot.failedAchievementRequests = 2U;
    snapshot.lastAchievementNetworkError = "password=never-copy-this";
    snapshot.cloudSyncState = "Enabled; idle";
    snapshot.cloudSyncSaves = true;
    snapshot.cloudSyncStates = true;
    snapshot.signedUpdateState = "Available; idle";
    snapshot.updateSigningKeyId = "704e04b184a939a4";
    snapshot.highestVerifiedUpdate = "1.2.3";
    snapshot.lastUpdateCheckUtc = "2026-09-02T18:00:00.000Z";
    snapshot.normalSpeedPercent = 125U;
    snapshot.slowMotionSpeedPercent = 50U;
    snapshot.fastForwardSpeedPercent = 800U;
    snapshot.activeSpeedPercent = 50U;
    snapshot.slowMotion = true;
    snapshot.loggerActive = true;
    snapshot.logger.writtenMessages = 42U;
    snapshot.bios.push_back({
      .name = "Sega CD USA",
      .status = "Valid",
      .sha256Prefix = "0123456789ab",
    });
    return snapshot;
  });
  window.show();
  auto* action = window.findChild<QAction*>(QStringLiteral("diagnosticsAction"));
  QVERIFY(action != nullptr);
  action->trigger();
  QApplication::processEvents();

  auto* dialog = window.findChild<genplusgx::ui::DiagnosticsDialog*>(
    QStringLiteral("diagnosticsDialog"));
  QVERIFY(dialog != nullptr);
  QVERIFY(dialog->isVisible());
  auto* report =
    dialog->findChild<QPlainTextEdit*>(QStringLiteral("diagnosticsReportEdit"));
  auto* copy = dialog->findChild<QPushButton*>(QStringLiteral("copyDiagnosticsButton"));
  QVERIFY(report != nullptr);
  QVERIFY(copy != nullptr);
  QVERIFY(report->isReadOnly());
  QVERIFY(!report->accessibleName().isEmpty());
  QVERIFY(!report->accessibleDescription().isEmpty());
  QVERIFY(report->toPlainText().contains(QStringLiteral("Test renderer generation 1")));
  QVERIFY(report->toPlainText().contains(QStringLiteral("Sega CD USA: Valid")));
  QVERIFY(report->toPlainText().contains(QStringLiteral("Controllers: 2")));
  QVERIFY(report->toPlainText().contains(QStringLiteral("Rewind: Enabled")));
  QVERIFY(report->toPlainText().contains(QStringLiteral("Rewind snapshots: 7")));
  QVERIFY(report->toPlainText().contains(
    QStringLiteral("Run-ahead: Enabled (active)")));
  QVERIFY(report->toPlainText().contains(
    QStringLiteral("Run-ahead speculative frames: 84")));
  QVERIFY(report->toPlainText().contains(
    QStringLiteral("Run-ahead state bytes: 196608 / 262144")));
  QVERIFY(report->toPlainText().contains(
    QStringLiteral("Netplay: Authenticated peer connected")));
  QVERIFY(report->toPlainText().contains(
    QStringLiteral("Netplay packets: 180 sent / 179 received")));
  QVERIFY(report->toPlainText().contains(
    QStringLiteral("Netplay output queue: 2 / 256")));
  QVERIFY(report->toPlainText().contains(
    QStringLiteral("Netplay prediction/rollback: 12 predicted, 2 requested, 2 performed")));
  QVERIFY(report->toPlainText().contains(
    QStringLiteral("RetroAchievements: Authenticated; recognized game active (Hardcore active)")));
  QVERIFY(report->toPlainText().contains(
    QStringLiteral("RetroAchievements bridge: 1 requests / 2 responses / 32 capacity")));
  QVERIFY(report->toPlainText().contains(
    QStringLiteral("RetroAchievements HTTPS: 0 active, 8 completed, 2 failed")));
  QVERIFY(report->toPlainText().contains(
    QStringLiteral("Cloud synchronization: Enabled; idle")));
  QVERIFY(report->toPlainText().contains(
    QStringLiteral("Cloud content: saves enabled, states enabled")));
  QVERIFY(report->toPlainText().contains(
    QStringLiteral("Update signing key: 704e04b184a939a4")));
  QVERIFY(report->toPlainText().contains(
    QStringLiteral("Last update attempt: 2026-09-02T18:00:00.000Z")));
  QVERIFY(report->toPlainText().contains(
    QStringLiteral("Configured speeds: Normal 125%, slow motion 50%, fast forward 800%")));
  QVERIFY(report->toPlainText().contains(
    QStringLiteral("Active speed: 50% (slow motion)")));
  QVERIFY(!report->toPlainText().contains(QStringLiteral("do-not-copy")));
  QVERIFY(!report->toPlainText().contains(QStringLiteral("never-copy-this")));
  QVERIFY(!report->toPlainText().contains(QStringLiteral("/users/private")));

  QTest::mouseClick(copy, Qt::LeftButton);
  QCOMPARE(QApplication::clipboard()->text(), report->toPlainText());

  generation = 2;
  action->trigger();
  QApplication::processEvents();
  QCOMPARE(window.findChildren<genplusgx::ui::DiagnosticsDialog*>().size(), 1);
  QVERIFY(report->toPlainText().contains(QStringLiteral("generation 2")));
  dialog->reject();
}

} // namespace

QTEST_MAIN(DiagnosticsDialogTest)
#include "diagnostics_dialog_test.moc"
