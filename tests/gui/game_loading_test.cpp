#include "genplusgx/emulation_worker.h"
#include "genplusgx/ui/dialog_service.h"
#include "genplusgx/ui/archive_entry_dialog.h"
#include "genplusgx/ui/main_window.h"
#include "genplusgx/video/display_widget.h"

#include "synthetic_rom.h"

#include <QAction>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QTest>
#include <QTemporaryDir>
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
  int chooseArchiveCount{0};
  std::optional<std::string> archiveSelection;
  std::vector<genplusgx::ArchivedGameEntry> archiveEntries;
  std::vector<QString> errors;

  std::optional<std::filesystem::path> chooseGame(
    QWidget*, const std::filesystem::path& initial) override
  {
    ++chooseCount;
    initialDirectory = initial;
    return selection;
  }

  std::optional<std::string> chooseArchiveEntry(
    QWidget*, const std::filesystem::path&,
    const std::vector<genplusgx::ArchivedGameEntry>& entries) override
  {
    ++chooseArchiveCount;
    archiveEntries = entries;
    return archiveSelection;
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
  void recentMenuLaunchesValidEntriesAndClears();
  void workerBackedLoadRunReplaceAndCloseWorkflow();
  void archiveBrowserExtractsTheSelectedGame();
  void archiveDialogHasStableKeyboardSemantics();
  void m3uPlaylistDrivesOrderedDiscNavigation();
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
  window.setGameLoadSink(
    [&requested](const auto& target) { requested = target.sourcePath; });
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

  window.setGameLoaded(fixture.path());
  window.showGameCloseError("Injected atomic save failure.");
  QVERIFY(window.isGameLoaded());
  QCOMPARE(window.loadedGamePath(), fixture.path());
  QCOMPARE(dialogs->errors.size(), std::size_t{1});
  QVERIFY(dialogs->errors.front().contains(QStringLiteral("remains loaded")));

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
  QVERIFY(dialogs->errors.front().contains(QStringLiteral("does not exist")));
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
  window.setGameLoadSink(
    [&requested](const auto& target) { requested = target.sourcePath; });

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

void GameLoadingTest::recentMenuLaunchesValidEntriesAndClears()
{
  genplusgx::test::TemporaryFixture fixture{
    genplusgx::test::makeGenesisRamMarkerRom(), ".md"};
  const auto missing = std::filesystem::temp_directory_path() /
    "genplusgx-missing-recent.gg";
  genplusgx::ui::MainWindow window;
  std::filesystem::path requested;
  window.setGameLoadSink(
    [&requested](const auto& target) { requested = target.sourcePath; });
  int clearCount = 0;
  window.setClearRecentGamesSink([&window, &clearCount] {
    ++clearCount;
    window.setRecentGames({});
    return genplusgx::PersistenceStatus{};
  });
  window.setRecentGames({fixture.path(), missing});

  auto* menu = window.findChild<QMenu*>(QStringLiteral("openRecentMenu"));
  QVERIFY(menu->isEnabled());
  auto* first = window.findChild<QAction*>(QStringLiteral("recentGameAction0"));
  auto* second = window.findChild<QAction*>(QStringLiteral("recentGameAction1"));
  auto* clear = window.findChild<QAction*>(QStringLiteral("clearRecentGamesAction"));
  QVERIFY(first != nullptr && first->isEnabled());
  QVERIFY(second != nullptr && !second->isEnabled());
  QVERIFY(second->text().contains(QStringLiteral("Missing")));
  QVERIFY(clear != nullptr && clear->isEnabled());

  first->trigger();
  QCOMPARE(requested, fixture.path());
  QVERIFY(window.isGameLoading());
  QVERIFY(!menu->isEnabled());
  window.setGameLoaded(fixture.path());
  QVERIFY(menu->isEnabled());
  clear->trigger();
  QApplication::processEvents();
  QCOMPARE(clearCount, 1);
  QVERIFY(!menu->isEnabled());
  QVERIFY(window.findChild<QAction*>(QStringLiteral("recentGameAction0")) == nullptr);
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
  window.setGameLoadSink([&worker, &operation](const auto& target) {
    QVERIFY(worker.submit(genplusgx::EmulationCommand::load(
      ++operation, target.runtimePath)));
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

void GameLoadingTest::archiveBrowserExtractsTheSelectedGame()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto root = genplusgx::ui::pathFromQString(temporary.path());
  const auto archivePath = root / "collection.zip";
  const auto genesis = genplusgx::test::makeGenesisRamMarkerRom();
  const auto sms = genplusgx::test::makeZ80RamMarkerRom();
  QVERIFY(genplusgx::test::writeZipFixture(archivePath, {
    {.name = "Genesis/Game.md", .data = genesis},
    {.name = "SMS/Game.sms", .data = sms},
  }));

  auto dialogs = std::make_shared<FakeDialogService>();
  dialogs->archiveSelection = "SMS/Game.sms";
  genplusgx::ui::MainWindow window;
  window.setDialogService(dialogs);
  window.setArchiveCacheDirectory(root / "cache" / "archives");
  std::optional<genplusgx::GameLaunchTarget> requested;
  window.setGameLoadSink(
    [&requested](const auto& target) { requested = target; });

  QVERIFY(window.requestGameLoad(archivePath));
  QCOMPARE(dialogs->chooseArchiveCount, 1);
  QCOMPARE(dialogs->archiveEntries.size(), std::size_t{2});
  QVERIFY(requested.has_value());
  QCOMPARE(requested->sourcePath, archivePath);
  QCOMPARE(requested->archiveEntry, std::string{"SMS/Game.sms"});
  QCOMPARE(requested->runtimePath.extension(), std::filesystem::path{".sms"});
  QCOMPARE(std::filesystem::file_size(requested->runtimePath), sms.size());
  window.setGameLoaded(*requested);
  QCOMPARE(window.loadedGamePath(), archivePath);
  QCOMPARE(window.loadedRuntimePath(), requested->runtimePath);
  QVERIFY(window.findChild<QLabel*>(QStringLiteral("gameStatusLabel"))->text()
    .contains(QStringLiteral("SMS/Game.sms")));

  window.setNoGameLoaded();
  dialogs->archiveSelection.reset();
  requested.reset();
  QVERIFY(!window.requestGameLoad(archivePath));
  QVERIFY(!requested.has_value());
  QVERIFY(dialogs->errors.empty());

  const auto singlePath = root / "single.zip";
  QVERIFY(genplusgx::test::writeZipFixture(singlePath, {
    {.name = "Only.md", .data = genesis},
  }));
  QVERIFY(window.requestGameLoad(singlePath));
  QCOMPARE(dialogs->chooseArchiveCount, 2);
  QVERIFY(requested && requested->archiveEntry == "Only.md");
}

void GameLoadingTest::archiveDialogHasStableKeyboardSemantics()
{
  std::string rawName{"Raw/"};
  rawName.push_back(static_cast<char>(0xffU));
  rawName += ".md";
  const std::vector<genplusgx::ArchivedGameEntry> entries{
    {.name = "A/Game.md", .compressedSize = 100U,
      .uncompressedSize = 64U * 1024U, .crc32 = 1U},
    {.name = "B/Game.sms", .compressedSize = 100U,
      .uncompressedSize = 32U * 1024U, .crc32 = 2U},
    {.name = rawName, .compressedSize = 100U,
      .uncompressedSize = 16U * 1024U, .crc32 = 3U},
  };
  genplusgx::ui::ArchiveEntryDialog dialog{"collection.zip", entries};
  QCOMPARE(dialog.objectName(), QStringLiteral("archiveEntryDialog"));
  auto* list = dialog.findChild<QListWidget*>(QStringLiteral("archiveEntryList"));
  auto* open = dialog.findChild<QPushButton*>(
    QStringLiteral("archiveEntryOpenButton"));
  auto* cancel = dialog.findChild<QPushButton*>(
    QStringLiteral("archiveEntryCancelButton"));
  QVERIFY(list != nullptr && open != nullptr && cancel != nullptr);
  QCOMPARE(list->count(), 3);
  QVERIFY(!list->accessibleName().isEmpty());
  list->setCurrentRow(2);
  QCOMPARE(dialog.selectedEntry(), std::optional<std::string>{rawName});
  list->setCurrentRow(1);
  QCOMPARE(dialog.selectedEntry(), std::optional<std::string>{"B/Game.sms"});
  QTest::mouseClick(open, Qt::LeftButton);
  QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
}

void GameLoadingTest::m3uPlaylistDrivesOrderedDiscNavigation()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto root = genplusgx::ui::pathFromQString(temporary.path());
  const auto first = root / "disc-one.iso";
  const auto second = root / "disc-two.iso";
  const auto playlist = root / "game.m3u";
  auto firstBytes = genplusgx::test::makeSegaCdDiscImage();
  auto secondBytes = firstBytes;
  secondBytes.back() = 0x5aU;
  {
    std::ofstream file(first, std::ios::binary);
    file.write(reinterpret_cast<const char*>(firstBytes.data()),
      static_cast<std::streamsize>(firstBytes.size()));
  }
  {
    std::ofstream file(second, std::ios::binary);
    file.write(reinterpret_cast<const char*>(secondBytes.data()),
      static_cast<std::streamsize>(secondBytes.size()));
  }
  {
    std::ofstream file(playlist, std::ios::binary);
    file << "#EXTM3U\ndisc-one.iso\ndisc-two.iso\n";
  }

  genplusgx::ui::MainWindow window;
  std::optional<genplusgx::GameLaunchTarget> requested;
  window.setGameLoadSink(
    [&requested](const auto& target) { requested = target; });
  QVERIFY(window.requestGameLoad(playlist));
  QVERIFY(requested && requested->isPlaylist());
  QCOMPARE(requested->sourcePath, playlist);
  QCOMPARE(requested->runtimePath, std::filesystem::weakly_canonical(first));
  QCOMPARE(requested->playlistDiscs.size(), std::size_t{2});
  window.setGameLoaded(*requested);

  std::vector<std::filesystem::path> changes;
  window.setDiscOperationSink(
    [&changes](auto operation, const auto& path, bool) {
      if (operation == genplusgx::ui::DiscUiOperation::change) {
        changes.push_back(path);
      }
    });
  window.setSegaCdSession(true, "USA", requested->playlistDiscs[0], false, true);
  auto* previous = window.findChild<QAction*>(
    QStringLiteral("previousDiscAction"));
  auto* next = window.findChild<QAction*>(QStringLiteral("nextDiscAction"));
  QVERIFY(previous != nullptr && next != nullptr);
  QVERIFY(!previous->isEnabled() && next->isEnabled());
  next->trigger();
  QCOMPARE(changes, std::vector<std::filesystem::path>{
    requested->playlistDiscs[1]});
  QVERIFY(!previous->isEnabled() && !next->isEnabled());

  window.setSegaCdSession(true, "USA", requested->playlistDiscs[1], false, true);
  QVERIFY(previous->isEnabled() && !next->isEnabled());
  previous->trigger();
  QCOMPARE(changes.size(), std::size_t{2});
  QCOMPARE(changes.back(), requested->playlistDiscs[0]);
}

} // namespace

QTEST_MAIN(GameLoadingTest)

#include "game_loading_test.moc"
