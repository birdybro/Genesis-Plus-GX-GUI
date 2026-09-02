#include "genplusgx/platform/physical_media.h"
#include "genplusgx/ui/main_window.h"
#include "genplusgx/ui/physical_media_dialog.h"

#include "physical_media_fixture.h"

#include <QAction>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest>

#include <filesystem>
#include <optional>
#include <string>

class PhysicalMediaGuiTest final : public QObject {
  Q_OBJECT

private slots:
  void physicalDiscDialogAndLaunchAreTypedAndRecoverable();
};

void PhysicalMediaGuiTest::physicalDiscDialogAndLaunchAreTypedAndRecoverable()
{
  using namespace genplusgx;
  using namespace genplusgx::platform;
  using namespace genplusgx::ui;

  MainWindow window;
  auto* open = window.findChild<QAction*>(
    QStringLiteral("openPhysicalDiscAction"));
  QVERIFY(open != nullptr);
  QVERIFY(!open->isEnabled());

  int discoveries = 0;
  int cancellations = 0;
  std::string selectedDrive;
  window.setPhysicalMediaActions({
    .discover = [&discoveries] { ++discoveries; },
    .importDisc = [&selectedDrive](const std::string& driveId) {
      selectedDrive = driveId;
    },
    .cancel = [&cancellations] { ++cancellations; },
  });
  window.setPhysicalMediaSupported(true);
  QVERIFY(open->isEnabled());
  open->trigger();
  auto* dialog = window.findChild<PhysicalMediaDialog*>(
    QStringLiteral("physicalMediaDialog"));
  QVERIFY(dialog != nullptr);
  QTRY_VERIFY(dialog->isVisible());
  QCOMPARE(discoveries, 1);
  QVERIFY(dialog->busy());

  const PhysicalDrive drive{
    .id = "test-optical-drive",
    .displayName = "External Test CD Drive",
  };
  window.setPhysicalMediaDrives({drive});
  auto* driveList = dialog->findChild<QListWidget*>(
    QStringLiteral("physicalMediaDriveList"));
  auto* import = dialog->findChild<QPushButton*>(
    QStringLiteral("physicalMediaImportButton"));
  auto* cancel = dialog->findChild<QPushButton*>(
    QStringLiteral("physicalMediaCancelButton"));
  auto* progress = dialog->findChild<QProgressBar*>(
    QStringLiteral("physicalMediaProgressBar"));
  auto* status = dialog->findChild<QLabel*>(
    QStringLiteral("physicalMediaStatusLabel"));
  QVERIFY(driveList != nullptr && import != nullptr && cancel != nullptr &&
    progress != nullptr && status != nullptr);
  QCOMPARE(driveList->count(), 1);
  QCOMPARE(driveList->currentRow(), 0);
  QVERIFY(import->isEnabled());
  QTest::mouseClick(import, Qt::LeftButton);
  QCOMPARE(selectedDrive, drive.id);
  QVERIFY(dialog->busy());
  window.setPhysicalMediaImportStarted();
  window.setPhysicalMediaImportProgress(75U, 150U);
  QCOMPARE(progress->value(), 50);
  QVERIFY(status->text().contains(QStringLiteral("75")));
  QVERIFY(cancel->isVisible() && cancel->isEnabled());
  QTest::mouseClick(cancel, Qt::LeftButton);
  QCOMPARE(cancellations, 1);
  window.showPhysicalMediaCancelled();
  QVERIFY(!dialog->busy());
  QVERIFY(status->text().contains(QStringLiteral("cancelled"),
    Qt::CaseInsensitive));

  window.setPhysicalMediaDrives({drive});
  QTest::mouseClick(import, Qt::LeftButton);
  window.setPhysicalMediaImportStarted();
  window.showPhysicalMediaError("Injected media error");
  QVERIFY(!dialog->busy());
  QVERIFY(status->text().contains(QStringLiteral("Injected media error")));

  QTemporaryDir cacheDirectory;
  QVERIFY(cacheDirectory.isValid());
  test::SyntheticPhysicalMediaBackend backend;
  const auto cache = std::filesystem::path{
    cacheDirectory.path().toStdString()} / "physical";
  const auto imported = snapshotPhysicalDisc(
    backend, backend.disc.drive.id, cache);
  QVERIFY(imported.status);
  std::optional<GameLaunchTarget> requested;
  window.setGameLoadSink(
    [&requested](const GameLaunchTarget& target) { requested = target; });
  QVERIFY(window.requestPhysicalMediaLoad(imported.snapshot));
  QVERIFY(requested.has_value());
  QVERIFY(requested->valid() && requested->isPhysicalMedia());
  QCOMPARE(requested->runtimePath, imported.snapshot.cuePath);
  QCOMPARE(requested->physicalMedia->displayName,
    backend.disc.drive.displayName);
  QCOMPARE(requested->physicalMedia->storageDirectory,
    imported.snapshot.storageDirectory);
  QVERIFY(!open->isEnabled());

  window.setGameLoaded(*requested);
  auto* gameStatus = window.findChild<QLabel*>(
    QStringLiteral("gameStatusLabel"));
  QVERIFY(gameStatus != nullptr);
  QVERIFY(gameStatus->text().contains(
    QString::fromStdString(backend.disc.drive.displayName)));
  QVERIFY(open->isEnabled());
  window.setNoGameLoaded();
  QVERIFY(releasePhysicalMediaSnapshot(cache, imported.snapshot));
  QVERIFY(!std::filesystem::exists(imported.snapshot.storageDirectory));

  window.setPhysicalMediaSupported(false);
  QVERIFY(!open->isEnabled());
}

QTEST_MAIN(PhysicalMediaGuiTest)
#include "physical_media_test.moc"
