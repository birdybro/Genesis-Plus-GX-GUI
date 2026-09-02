#include "genplusgx/ui/game_information_dialog.h"
#include "genplusgx/ui/game_library_dialog.h"
#include "genplusgx/ui/main_window.h"

#include "synthetic_rom.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

class FakeDialogService final : public genplusgx::ui::DialogService {
public:
  std::optional<std::filesystem::path> directorySelection;
  std::optional<std::filesystem::path> artworkSelection;
  std::vector<QString> errors;

  std::optional<std::filesystem::path> chooseGame(
    QWidget*, const std::filesystem::path&) override
  {
    return std::nullopt;
  }

  std::optional<std::filesystem::path> chooseDirectory(
    QWidget*, const std::filesystem::path&) override
  {
    return directorySelection;
  }

  std::optional<std::filesystem::path> chooseArtwork(
    QWidget*, const std::filesystem::path&) override
  {
    return artworkSelection;
  }

  void showError(QWidget*, const QString& title, const QString& message) override
  {
    errors.push_back(title + QStringLiteral(": ") + message);
  }
};

genplusgx::library::LibraryGame game(
  std::int64_t id,
  std::filesystem::path path,
  std::string title,
  genplusgx::library::GameSystem system,
  std::string region,
  bool favorite,
  std::uint64_t playCount,
  std::optional<std::int64_t> lastPlayed,
  std::filesystem::path artwork = {})
{
  genplusgx::library::LibraryGame result;
  result.id = id;
  result.directoryId = 10;
  result.metadata.path = std::move(path);
  result.metadata.internationalTitle = std::move(title);
  result.metadata.system = system;
  result.metadata.region = std::move(region);
  result.metadata.sha256 = std::string(64, static_cast<char>('0' + id));
  result.favorite = favorite;
  result.playCount = playCount;
  result.lastPlayedEpochMilliseconds = lastPlayed;
  result.artworkPath = std::move(artwork);
  return result;
}

genplusgx::library::OnlineMetadataRecord onlineRecord(const std::string& hash)
{
  return {
    .lookupSha256 = hash,
    .providerName = "Fixture Provider",
    .providerHomepage = "https://provider.example.test",
    .preferredTitle = "Enriched Beta CD",
    .alternateTitle = "Beta Alternate",
    .description = "Licensed fixture description.",
    .releaseDate = "1994-01-02",
    .developer = "Fixture Studio",
    .publisher = "Fixture Publisher",
    .genres = {"Role Playing"},
    .attribution = {
      .creator = "Fixture contributors",
      .licenseSpdx = "CC-BY-4.0",
      .licenseUrl = "https://creativecommons.org/licenses/by/4.0/",
      .sourceUrl = "https://provider.example.test/games/fixture",
    },
    .artwork = std::nullopt,
  };
}

class GameLibraryDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void fullOfflineLibraryWorkflow();
  void databaseScannerWorkflow();
};

void GameLibraryDialogTest::fullOfflineLibraryWorkflow()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const std::filesystem::path root{temporary.path().toStdString()};
  const auto artwork = root / "cover.png";
  QImage cover{32, 48, QImage::Format_ARGB32};
  cover.fill(QColor{20, 100, 180});
  QVERIFY(cover.save(QString::fromStdString(artwork.string())));

  auto dialogs = std::make_shared<FakeDialogService>();
  dialogs->directorySelection = root / "new-games";
  dialogs->artworkSelection = artwork;
  genplusgx::ui::MainWindow window;
  window.setDialogService(dialogs);
  auto metadataSettings = genplusgx::library::defaultOnlineMetadataSettings();
  metadataSettings.enabled = true;
  window.setOnlineMetadataSettings(metadataSettings);
  std::vector<std::pair<std::filesystem::path, bool>> added;
  std::vector<std::int64_t> removed;
  std::vector<std::int64_t> scanned;
  std::vector<std::pair<std::int64_t, bool>> recursiveUpdates;
  std::vector<std::pair<std::int64_t, bool>> favoriteUpdates;
  std::vector<std::pair<std::int64_t, std::filesystem::path>> artworkUpdates;
  std::vector<std::int64_t> metadataLookups;
  std::vector<std::int64_t> metadataClears;
  std::vector<std::pair<std::int64_t, std::filesystem::path>> launches;
  window.setGameLibraryActions({
    .addDirectory = [&added](const auto& path, bool recursive) {
      added.emplace_back(path, recursive);
    },
    .removeDirectory = [&removed](auto id) { removed.push_back(id); },
    .updateDirectory = [&recursiveUpdates](auto id, bool recursive) {
      recursiveUpdates.emplace_back(id, recursive);
    },
    .scanDirectory = [&scanned](auto id) { scanned.push_back(id); },
    .setFavorite = [&favoriteUpdates](auto id, bool favorite) {
      favoriteUpdates.emplace_back(id, favorite);
    },
    .setArtwork = [&artworkUpdates](auto id, const auto& path) {
      artworkUpdates.emplace_back(id, path);
    },
    .lookupOnlineMetadata = [&metadataLookups](auto id) {
      metadataLookups.push_back(id);
    },
    .clearOnlineMetadata = [&metadataClears](auto id) {
      metadataClears.push_back(id);
    },
    .launchGame = [&launches](auto id, const auto& path) {
      launches.emplace_back(id, path);
    },
  });
  std::vector<genplusgx::library::LibraryDirectory> directories{
    {.id = 10, .path = root, .recursive = true}};
  std::vector<genplusgx::library::LibraryGame> games{
    game(1, root / "alpha.md", "Alpha Drive",
      genplusgx::library::GameSystem::genesis, "USA", false, 2, 1'700'000'000'000),
    game(2, root / "beta.cue", "Beta CD",
      genplusgx::library::GameSystem::segaCd, "Europe", true, 7,
      1'710'000'000'000, artwork),
  };
  games[1].onlineMetadata = onlineRecord(games[1].metadata.sha256);
  window.setGameLibrarySnapshot(directories, games);
  window.show();
  window.findChild<QAction*>(QStringLiteral("gameLibraryAction"))->trigger();
  QApplication::processEvents();

  auto* dialog = window.findChild<genplusgx::ui::GameLibraryDialog*>(
    QStringLiteral("gameLibraryDialog"));
  QVERIFY(dialog != nullptr);
  QVERIFY(dialog->isVisible());
  auto* table = dialog->findChild<QTableView*>(QStringLiteral("libraryGameTable"));
  auto* search = dialog->findChild<QLineEdit*>(QStringLiteral("librarySearchEdit"));
  auto* system = dialog->findChild<QComboBox*>(
    QStringLiteral("librarySystemFilterCombo"));
  auto* region = dialog->findChild<QComboBox*>(
    QStringLiteral("libraryRegionFilterCombo"));
  auto* favorites = dialog->findChild<QCheckBox*>(
    QStringLiteral("libraryFavoritesOnlyCheck"));
  QVERIFY(table != nullptr && search != nullptr && system != nullptr &&
    region != nullptr && favorites != nullptr);
  QCOMPARE(table->model()->rowCount(), 2);

  search->setText(QStringLiteral("alpha"));
  QCOMPARE(table->model()->rowCount(), 1);
  search->clear();
  system->setCurrentIndex(system->findData(
    static_cast<int>(genplusgx::library::GameSystem::segaCd)));
  QCOMPARE(table->model()->rowCount(), 1);
  system->setCurrentIndex(0);
  region->setCurrentIndex(region->findData(QStringLiteral("USA")));
  QCOMPARE(table->model()->rowCount(), 1);
  region->setCurrentIndex(0);
  favorites->setChecked(true);
  QCOMPARE(table->model()->rowCount(), 1);
  favorites->setChecked(false);
  table->sortByColumn(5, Qt::DescendingOrder);
  QApplication::processEvents();
  QCOMPARE(table->model()->index(0, 5).data().toString(), QStringLiteral("7"));

  table->selectRow(0);
  QApplication::processEvents();
  auto* artworkLabel = dialog->findChild<QLabel*>(
    QStringLiteral("libraryArtworkLabel"));
  QVERIFY(artworkLabel != nullptr);
  QVERIFY(artworkLabel->text().isEmpty());
  auto* metadataDetails = dialog->findChild<QLabel*>(
    QStringLiteral("libraryOnlineMetadataDetailsLabel"));
  auto* lookupMetadata = dialog->findChild<QPushButton*>(
    QStringLiteral("libraryLookupMetadataButton"));
  auto* clearMetadata = dialog->findChild<QPushButton*>(
    QStringLiteral("libraryClearMetadataButton"));
  QVERIFY(metadataDetails != nullptr);
  QVERIFY(lookupMetadata != nullptr);
  QVERIFY(clearMetadata != nullptr);
  QVERIFY(metadataDetails->text().contains(QStringLiteral("Fixture Provider")));
  QVERIFY(metadataDetails->text().contains(QStringLiteral("CC-BY-4.0")));
  QVERIFY(lookupMetadata->isEnabled());
  QVERIFY(clearMetadata->isEnabled());
  QTest::mouseClick(lookupMetadata, Qt::LeftButton);
  QCOMPARE(metadataLookups, std::vector<std::int64_t>{2});
  dialog->showOnlineMetadataStarted(2);
  QVERIFY(!lookupMetadata->isEnabled());
  QVERIFY(dialog->findChild<QProgressBar*>(
    QStringLiteral("libraryProgressBar"))->isVisible());
  dialog->showOnlineMetadataCompleted(2, true, false);
  QVERIFY(dialog->findChild<QLabel*>(QStringLiteral("libraryStatusLabel"))
    ->text().contains(QStringLiteral("cache"), Qt::CaseInsensitive));
  QVERIFY(lookupMetadata->isEnabled());
  QTest::mouseClick(clearMetadata, Qt::LeftButton);
  QCOMPARE(metadataClears, std::vector<std::int64_t>{2});
  QTest::mouseClick(dialog->findChild<QPushButton*>(
    QStringLiteral("libraryFavoriteButton")), Qt::LeftButton);
  QCOMPARE(favoriteUpdates.size(), std::size_t{1});
  QCOMPARE(favoriteUpdates.back(), (std::pair<std::int64_t, bool>{2, false}));
  QTest::mouseClick(dialog->findChild<QPushButton*>(
    QStringLiteral("libraryLaunchButton")), Qt::LeftButton);
  QCOMPARE(launches.size(), std::size_t{1});
  QCOMPARE(launches.back().first, 2);
  QCOMPARE(launches.back().second, root / "beta.cue");
  QTest::mouseClick(dialog->findChild<QPushButton*>(
    QStringLiteral("libraryInformationButton")), Qt::LeftButton);
  QApplication::processEvents();
  QVERIFY(dialog->findChild<genplusgx::ui::GameInformationDialog*>(
    QStringLiteral("gameInformationDialog")) != nullptr);
  auto* information = dialog->findChild<genplusgx::ui::GameInformationDialog*>(
    QStringLiteral("gameInformationDialog"));
  QCOMPARE(information->findChild<QLineEdit*>(
    QStringLiteral("gameInfoOnlineTitleValue"))->text(),
    QStringLiteral("Enriched Beta CD"));
  QCOMPARE(information->findChild<QLineEdit*>(
    QStringLiteral("gameInfoOnlineLicenseValue"))->text(),
    QStringLiteral("CC-BY-4.0"));
  QTest::mouseClick(dialog->findChild<QPushButton*>(
    QStringLiteral("libraryChooseArtworkButton")), Qt::LeftButton);
  QCOMPARE(artworkUpdates.back(),
    (std::pair<std::int64_t, std::filesystem::path>{2, artwork}));
  QTest::mouseClick(dialog->findChild<QPushButton*>(
    QStringLiteral("libraryClearArtworkButton")), Qt::LeftButton);
  QVERIFY(artworkUpdates.back().second.empty());

  auto* directoryList = dialog->findChild<QListWidget*>(
    QStringLiteral("libraryDirectoryList"));
  directoryList->setCurrentRow(0);
  auto* recursive = dialog->findChild<QCheckBox*>(
    QStringLiteral("libraryRecursiveCheck"));
  QVERIFY(recursive->isChecked());
  QTest::mouseClick(recursive, Qt::LeftButton);
  QCOMPARE(recursiveUpdates.back(), (std::pair<std::int64_t, bool>{10, false}));
  QTest::mouseClick(dialog->findChild<QPushButton*>(
    QStringLiteral("libraryScanDirectoryButton")), Qt::LeftButton);
  QCOMPARE(scanned.back(), 10);
  QTest::mouseClick(dialog->findChild<QPushButton*>(
    QStringLiteral("libraryRemoveDirectoryButton")), Qt::LeftButton);
  QCOMPARE(removed.back(), 10);
  QTest::mouseClick(dialog->findChild<QPushButton*>(
    QStringLiteral("libraryAddDirectoryButton")), Qt::LeftButton);
  QCOMPARE(added.back(),
    (std::pair<std::filesystem::path, bool>{root / "new-games", true}));

  dialog->showScanStarted(10, root);
  QVERIFY(dialog->findChild<QProgressBar*>(
    QStringLiteral("libraryProgressBar"))->isVisible());
  QVERIFY(!dialog->findChild<QPushButton*>(
    QStringLiteral("libraryFavoriteButton"))->isEnabled());
  dialog->showScanProgress(10, {
    .visitedFiles = 12,
    .supportedFiles = 2,
    .indexedGames = 2,
  });
  QVERIFY(dialog->findChild<QLabel*>(QStringLiteral("libraryStatusLabel"))
    ->text().contains(QStringLiteral("12")));
  dialog->showScanCompleted(10, {
    .visitedFiles = 12,
    .supportedFiles = 2,
    .indexedGames = 2,
  });
  QVERIFY(!dialog->findChild<QProgressBar*>(
    QStringLiteral("libraryProgressBar"))->isVisible());
  dialog->showScanStarted(10, root);
  dialog->showScanFailed(10, "synthetic scan failure");
  QCOMPARE(dialogs->errors.size(), std::size_t{1});
  QVERIFY(dialogs->errors.back().contains(QStringLiteral("synthetic scan failure")));
}

void GameLibraryDialogTest::databaseScannerWorkflow()
{
  using namespace std::chrono_literals;
  using namespace genplusgx::library;
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const std::filesystem::path base{temporary.path().toStdString()};
  const auto root = base / "games";
  const auto romPath = root / "indexed.md";
  std::error_code error;
  std::filesystem::create_directories(root, error);
  QVERIFY(!error);
  const auto bytes = genplusgx::test::makeGenesisRamMarkerRom();
  std::ofstream rom{romPath, std::ios::binary | std::ios::trunc};
  rom.write(reinterpret_cast<const char*>(bytes.data()),
    static_cast<std::streamsize>(bytes.size()));
  rom.close();
  QVERIFY(static_cast<bool>(rom));

  GameLibraryDatabase database{base / "library.sqlite3"};
  QVERIFY(database.initialize());
  GameLibraryScanner scanner{database.path()};
  QVERIFY(scanner.start());
  QVERIFY(scanner.waitForEvent(2s).has_value());
  auto dialogs = std::make_shared<FakeDialogService>();
  dialogs->directorySelection = root;
  genplusgx::ui::MainWindow window;
  window.setDialogService(dialogs);
  std::int64_t directoryId = 0;
  const auto refresh = [&] {
    const auto directories = database.directories();
    const auto indexedGames = database.games();
    QVERIFY(directories.status);
    QVERIFY(indexedGames.status);
    window.setGameLibrarySnapshot(
      directories.directories, indexedGames.games);
  };
  window.setGameLibraryActions({
    .addDirectory = [&](const auto& path, bool recursive) {
      const auto added = database.addDirectory(path, recursive);
      QVERIFY(added.status);
      directoryId = added.directory.id;
      refresh();
      QVERIFY(scanner.requestScan(100, directoryId));
    },
    .removeDirectory = [&](auto id) {
      QVERIFY(database.removeDirectory(id));
      refresh();
    },
    .updateDirectory = [&](auto id, bool recursive) {
      QVERIFY(database.updateDirectory(id, recursive));
      refresh();
    },
    .scanDirectory = [&](auto id) { QVERIFY(scanner.requestScan(101, id)); },
    .setFavorite = [&](auto id, bool favorite) {
      QVERIFY(database.setFavorite(id, favorite));
      refresh();
    },
    .setArtwork = [&](auto id, const auto& path) {
      QVERIFY(database.setArtworkPath(id, path));
      refresh();
    },
    .lookupOnlineMetadata = [](auto) {},
    .clearOnlineMetadata = [](auto) {},
    .launchGame = [&](auto id, const auto&) {
      QVERIFY(database.recordLaunch(id, 1'720'000'000'000));
      refresh();
    },
  });
  refresh();
  window.show();
  window.findChild<QAction*>(QStringLiteral("gameLibraryAction"))->trigger();
  QApplication::processEvents();
  auto* dialog = window.findChild<genplusgx::ui::GameLibraryDialog*>(
    QStringLiteral("gameLibraryDialog"));
  QVERIFY(dialog != nullptr);
  QTest::mouseClick(dialog->findChild<QPushButton*>(
    QStringLiteral("libraryAddDirectoryButton")), Qt::LeftButton);
  QVERIFY(directoryId > 0);

  std::optional<GameLibraryScanEvent> completed;
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < deadline && !completed) {
    const auto event = scanner.waitForEvent(20ms);
    if (event && event->type == GameLibraryScanEventType::scanCompleted) {
      completed = event;
    }
    QApplication::processEvents();
  }
  QVERIFY(completed.has_value());
  refresh();
  auto* table = dialog->findChild<QTableView*>(QStringLiteral("libraryGameTable"));
  QCOMPARE(table->model()->rowCount(), 1);
  QCOMPARE(database.games().games.front().metadata.path, romPath);

  table->selectRow(0);
  QTest::mouseClick(dialog->findChild<QPushButton*>(
    QStringLiteral("libraryFavoriteButton")), Qt::LeftButton);
  QVERIFY(database.games().games.front().favorite);
  dialog->findChild<QCheckBox*>(
    QStringLiteral("libraryFavoritesOnlyCheck"))->setChecked(true);
  QCOMPARE(table->model()->rowCount(), 1);
  table->selectRow(0);
  QTest::mouseClick(dialog->findChild<QPushButton*>(
    QStringLiteral("libraryLaunchButton")), Qt::LeftButton);
  const auto launched = database.games().games.front();
  QCOMPARE(launched.playCount, std::uint64_t{1});
  QCOMPARE(launched.lastPlayedEpochMilliseconds,
    std::optional<std::int64_t>{1'720'000'000'000});

  dialog->findChild<QListWidget*>(
    QStringLiteral("libraryDirectoryList"))->setCurrentRow(0);
  QTest::mouseClick(dialog->findChild<QPushButton*>(
    QStringLiteral("libraryRemoveDirectoryButton")), Qt::LeftButton);
  QCOMPARE(database.directories().directories.size(), std::size_t{0});
  QCOMPARE(database.games().games.size(), std::size_t{0});
  QCOMPARE(table->model()->rowCount(), 0);
  QVERIFY(scanner.stop());
}

} // namespace

QTEST_MAIN(GameLibraryDialogTest)
#include "game_library_dialog_test.moc"
