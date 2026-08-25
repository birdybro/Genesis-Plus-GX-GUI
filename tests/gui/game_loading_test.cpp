#include "genplusgx/emulation_worker.h"
#include "genplusgx/ui/dialog_service.h"
#include "genplusgx/ui/main_window.h"
#include "genplusgx/video/display_widget.h"

#include "synthetic_rom.h"

#include <QAction>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLabel>
#include <QMimeData>
#include <QTest>
#include <QUrl>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace std::chrono_literals;

namespace {

class FakeDialogService final : public genplusgx::ui::DialogService {
public:
  std::optional<std::filesystem::path> selection;
  std::filesystem::path initialDirectory;
  int chooseCount{0};
  std::vector<QString> errors;

  std::optional<std::filesystem::path> chooseGame(
    QWidget*, const std::filesystem::path& initial) override
  {
    ++chooseCount;
    initialDirectory = initial;
    return selection;
  }

  void showError(QWidget*, const QString& title, const QString& message) override
  {
    errors.push_back(title + QStringLiteral(": ") + message);
  }
};

std::optional<genplusgx::EmulationEvent> waitForOperation(
  genplusgx::EmulationWorker& worker,
  std::uint64_t operationId)
{
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = worker.waitForEvent(100ms);
    if (event && event->operationId == operationId) {
      return event;
    }
  }
  return std::nullopt;
}

class GameLoadingTest final : public QObject {
  Q_OBJECT

private slots:
  void openDialogAndCloseActionUseInjectedServices();
  void invalidSelectionReportsWithoutDispatching();
  void dragAndDropAcceptsOneSupportedLocalFile();
  void workerBackedLoadRunReplaceAndCloseWorkflow();
};

void GameLoadingTest::openDialogAndCloseActionUseInjectedServices()
{
  genplusgx::test::TemporaryFixture fixture{
    genplusgx::test::makeGenesisRamMarkerRom(), ".md"};
  auto dialogs = std::make_shared<FakeDialogService>();
  dialogs->selection = fixture.path();
  genplusgx::ui::MainWindow window;
  window.setDialogService(dialogs);
  std::filesystem::path requested;
  int closeCount = 0;
  window.setGameLoadSink([&requested](const auto& path) { requested = path; });
  window.setGameCloseSink([&closeCount] { ++closeCount; });

  window.findChild<QAction*>(QStringLiteral("openGameAction"))->trigger();
  QCOMPARE(dialogs->chooseCount, 1);
  QCOMPARE(requested, fixture.path());
  QVERIFY(window.isGameLoading());
  QVERIFY(!window.findChild<QAction*>(QStringLiteral("openGameAction"))->isEnabled());
  QVERIFY(window.findChild<QLabel*>(QStringLiteral("gameStatusLabel"))->text()
    .contains(QStringLiteral("Loading")));

  window.setGameLoaded(fixture.path());
  QVERIFY(window.isGameLoaded());
  QCOMPARE(window.loadedGamePath(), fixture.path());
  QVERIFY(window.findChild<QAction*>(QStringLiteral("closeGameAction"))->isEnabled());
  window.findChild<QAction*>(QStringLiteral("closeGameAction"))->trigger();
  QCOMPARE(closeCount, 1);
  QVERIFY(window.isGameLoading());

  window.setNoGameLoaded();
  QVERIFY(!window.isGameLoaded());
  QVERIFY(window.loadedGamePath().empty());
  QVERIFY(!window.findChild<QAction*>(QStringLiteral("closeGameAction"))->isEnabled());
}

void GameLoadingTest::invalidSelectionReportsWithoutDispatching()
{
  auto dialogs = std::make_shared<FakeDialogService>();
  dialogs->selection = std::filesystem::temp_directory_path() / "unsupported.zip";
  genplusgx::ui::MainWindow window;
  window.setDialogService(dialogs);
  int dispatchCount = 0;
  window.setGameLoadSink([&dispatchCount](const auto&) { ++dispatchCount; });

  window.findChild<QAction*>(QStringLiteral("openGameAction"))->trigger();
  QCOMPARE(dispatchCount, 0);
  QCOMPARE(dialogs->errors.size(), std::size_t{1});
  QVERIFY(dialogs->errors.front().contains(QStringLiteral("not supported")));
  QVERIFY(!window.isGameLoading());

  dialogs->selection.reset();
  window.findChild<QAction*>(QStringLiteral("openGameAction"))->trigger();
  QCOMPARE(dialogs->chooseCount, 2);
  QCOMPARE(dispatchCount, 0);

  genplusgx::test::TemporaryFixture loadedFixture{
    genplusgx::test::makeGenesisRamMarkerRom(), ".md"};
  window.setGameLoaded(loadedFixture.path());
  dialogs->selection = std::filesystem::temp_directory_path() / "still-unsupported.zip";
  window.findChild<QAction*>(QStringLiteral("openGameAction"))->trigger();
  QVERIFY(window.isGameLoaded());
  QCOMPARE(window.loadedGamePath(), loadedFixture.path());
}

void GameLoadingTest::dragAndDropAcceptsOneSupportedLocalFile()
{
  genplusgx::test::TemporaryFixture fixture{
    genplusgx::test::makeGenesisRamMarkerRom(), ".gen"};
  genplusgx::ui::MainWindow window;
  window.show();
  std::filesystem::path requested;
  window.setGameLoadSink([&requested](const auto& path) { requested = path; });

  QMimeData validMime;
  validMime.setUrls({QUrl::fromLocalFile(
    genplusgx::ui::pathToQString(fixture.path()))});
  QDragEnterEvent drag(
    QPoint{10, 10}, Qt::CopyAction, &validMime, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&window, &drag);
  QVERIFY(drag.isAccepted());
  QDropEvent drop(
    QPointF{10.0, 10.0}, Qt::CopyAction, &validMime, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&window, &drop);
  QVERIFY(drop.isAccepted());
  QCOMPARE(requested, fixture.path());

  window.setNoGameLoaded();
  requested.clear();
  QMimeData multipleMime;
  multipleMime.setUrls({
    QUrl::fromLocalFile(genplusgx::ui::pathToQString(fixture.path())),
    QUrl::fromLocalFile(genplusgx::ui::pathToQString(fixture.path()))});
  QDropEvent multiple(
    QPointF{10.0, 10.0}, Qt::CopyAction, &multipleMime, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&window, &multiple);
  QVERIFY(!multiple.isAccepted());
  QVERIFY(requested.empty());
}

void GameLoadingTest::workerBackedLoadRunReplaceAndCloseWorkflow()
{
  genplusgx::test::TemporaryFixture first{
    genplusgx::test::makeGenesisRamMarkerRom(), ".md"};
  genplusgx::test::TemporaryFixture second{
    genplusgx::test::makeGenesisRamMarkerRom(), ".gen"};
  auto dialogs = std::make_shared<FakeDialogService>();
  genplusgx::ui::MainWindow window;
  window.setDialogService(dialogs);
  auto frames = std::make_shared<genplusgx::VideoFrameExchange>();
  window.displayWidget()->setFrameExchange(frames);
  genplusgx::EmulationWorker worker{64U, 64U, 48'000, frames};
  QVERIFY(worker.start());
  QVERIFY(worker.waitForEvent(2s).has_value());

  std::uint64_t operation = 100U;
  window.setGameLoadSink([&worker, &operation](const auto& path) {
    QVERIFY(worker.submit(genplusgx::EmulationCommand::load(++operation, path)));
  });
  QVERIFY(window.requestGameLoad(first.path()));
  auto loaded = waitForOperation(worker, operation);
  QVERIFY(loaded.has_value());
  QVERIFY(loaded->succeeded());
  window.setGameLoaded(first.path());
  QVERIFY(worker.submit(genplusgx::EmulationCommand::simple(
    genplusgx::EmulationCommandType::start, ++operation)));
  auto started = waitForOperation(worker, operation);
  QVERIFY(started && started->succeeded());

  bool receivedFrame = false;
  const auto frameDeadline = std::chrono::steady_clock::now() + 2s;
  while (!receivedFrame && std::chrono::steady_clock::now() < frameDeadline) {
    const auto event = worker.waitForEvent(100ms);
    receivedFrame = event &&
      event->type == genplusgx::EmulationEventType::frameCompleted;
  }
  QVERIFY(receivedFrame);
  QVERIFY(window.displayWidget()->presentLatestFrame());
  QVERIFY(window.displayWidget()->hasFrame());

  QVERIFY(window.requestGameLoad(second.path()));
  loaded = waitForOperation(worker, operation);
  QVERIFY(loaded && loaded->succeeded());
  window.setGameLoaded(second.path());
  QCOMPARE(window.loadedGamePath(), second.path());

  window.setGameCloseSink([&worker, &operation] {
    QVERIFY(worker.submit(genplusgx::EmulationCommand::simple(
      genplusgx::EmulationCommandType::unloadGame, ++operation)));
  });
  window.findChild<QAction*>(QStringLiteral("closeGameAction"))->trigger();
  const auto unloaded = waitForOperation(worker, operation);
  QVERIFY(unloaded && unloaded->succeeded());
  window.setNoGameLoaded();
  QVERIFY(!window.displayWidget()->hasFrame());

  genplusgx::test::TemporaryFixture malformed{{}, ".md"};
  QVERIFY(window.requestGameLoad(malformed.path()));
  const auto rejected = waitForOperation(worker, operation);
  QVERIFY(rejected && !rejected->succeeded());
  window.showGameLoadError(malformed.path(), rejected->message);
  QVERIFY(!window.isGameLoaded());
  QCOMPARE(dialogs->errors.size(), std::size_t{1});
  QVERIFY(dialogs->errors.front().contains(QStringLiteral("could not load"),
    Qt::CaseInsensitive));
  QVERIFY(worker.stop());
}

} // namespace

QTEST_MAIN(GameLoadingTest)

#include "game_loading_test.moc"
