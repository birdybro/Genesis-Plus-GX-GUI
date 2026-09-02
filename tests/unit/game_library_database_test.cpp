#include "genplusgx/library/game_library_database.h"

#include "synthetic_rom.h"

#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::filesystem::path pathIn(
  const QTemporaryDir& directory,
  std::string_view name)
{
  return std::filesystem::path{directory.path().toStdString()} / std::string{name};
}

bool writeBytes(
  const std::filesystem::path& path,
  std::span<const std::uint8_t> bytes)
{
  std::ofstream stream{path, std::ios::binary | std::ios::trunc};
  stream.write(
    reinterpret_cast<const char*>(bytes.data()),
    static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(stream);
}

bool writeText(const std::filesystem::path& path, std::string_view text)
{
  return writeBytes(path, {
    reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

bool executeSql(const std::filesystem::path& path, const QString& statement)
{
  const auto connection = QStringLiteral("library-schema-manipulation-test");
  bool succeeded = false;
  {
    auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(QString::fromStdString(path.string()));
    if (database.open()) {
      QSqlQuery query{database};
      succeeded = query.exec(statement);
      database.close();
    }
  }
  QSqlDatabase::removeDatabase(connection);
  return succeeded;
}

bool setSchemaVersion(const std::filesystem::path& path, unsigned int version)
{
  return executeSql(
    path, QStringLiteral("PRAGMA user_version = %1").arg(version));
}

bool createLegacyVersionOneDatabase(const std::filesystem::path& path)
{
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return false;
  }
  const auto connection = QStringLiteral("library-v1-migration-test");
  bool succeeded = false;
  {
    auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(QString::fromStdString(path.string()));
    if (database.open()) {
      QSqlQuery query{database};
      succeeded = query.exec(QStringLiteral(
        "CREATE TABLE library_directories("
        "id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE, "
        "recursive INTEGER NOT NULL CHECK(recursive IN (0,1)), "
        "created_at INTEGER NOT NULL)")) &&
        query.exec(QStringLiteral(
        "CREATE TABLE library_games("
        "id INTEGER PRIMARY KEY, directory_id INTEGER NOT NULL "
        "REFERENCES library_directories(id) ON DELETE CASCADE, "
        "path TEXT NOT NULL, related_data_path TEXT NOT NULL DEFAULT '', "
        "file_name TEXT NOT NULL, display_title TEXT NOT NULL, "
        "file_size INTEGER NOT NULL, system INTEGER NOT NULL, format TEXT NOT NULL, "
        "domestic_title TEXT NOT NULL DEFAULT '', "
        "international_title TEXT NOT NULL DEFAULT '', copyright TEXT NOT NULL DEFAULT '', "
        "product_code TEXT NOT NULL DEFAULT '', region TEXT NOT NULL DEFAULT '', "
        "rom_type TEXT NOT NULL DEFAULT '', peripheral_support TEXT NOT NULL DEFAULT '', "
        "mapper TEXT NOT NULL DEFAULT '', sha256 TEXT NOT NULL, notes TEXT NOT NULL DEFAULT '', "
        "header_checksum INTEGER, computed_checksum INTEGER, declared_rom_size INTEGER, "
        "track_count INTEGER NOT NULL DEFAULT 0, header_recognized INTEGER NOT NULL, "
        "last_modified INTEGER NOT NULL, favorite INTEGER NOT NULL DEFAULT 0, "
        "last_played INTEGER, play_count INTEGER NOT NULL DEFAULT 0, "
        "artwork_path TEXT NOT NULL DEFAULT '', scan_generation INTEGER NOT NULL, "
        "UNIQUE(directory_id, path))")) &&
        query.exec(QStringLiteral("PRAGMA user_version = 1"));
      database.close();
    }
  }
  QSqlDatabase::removeDatabase(connection);
  return succeeded;
}

genplusgx::library::OnlineMetadataRecord onlineRecord(const std::string& hash)
{
  return {
    .lookupSha256 = hash,
    .providerName = "Fixture Provider",
    .providerHomepage = "https://provider.example.test",
    .preferredTitle = "Enriched Fixture Title",
    .alternateTitle = "Fixture Alternate",
    .description = "Licensed fixture description.",
    .releaseDate = "1994-01-02",
    .developer = "Fixture Studio",
    .publisher = "Fixture Publisher",
    .genres = {"Adventure"},
    .attribution = {
      .creator = "Fixture contributors",
      .licenseSpdx = "CC-BY-4.0",
      .licenseUrl = "https://creativecommons.org/licenses/by/4.0/",
      .sourceUrl = "https://provider.example.test/games/fixture",
    },
    .artwork = std::nullopt,
  };
}

} // namespace

int main(int argc, char* argv[])
{
  QCoreApplication application{argc, argv};
  using namespace genplusgx::library;
  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "Temporary directory was unavailable")) {
    return 1;
  }
  const auto databasePath = pathIn(temporary, "library/game-library.sqlite3");
  const auto gamesPath = pathIn(temporary, "games");
  const auto nestedPath = gamesPath / "nested";
  std::error_code error;
  std::filesystem::create_directories(nestedPath, error);
  if (!check(!error, "Test game directories could not be created")) {
    return 2;
  }
  const auto romPath = gamesPath / "fixture.md";
  const auto artworkPath = gamesPath / "fixture.png";
  const auto romBytes = genplusgx::test::makeGenesisSramWriterRom();
  constexpr std::array<std::uint8_t, 8> artworkBytes{
    0x89U, 0x50U, 0x4eU, 0x47U, 0x0dU, 0x0aU, 0x1aU, 0x0aU};
  if (!check(writeBytes(romPath, romBytes) &&
      writeBytes(artworkPath, artworkBytes),
      "Test ROM/artwork files could not be written")) {
    return 3;
  }
  const auto metadata = readGameMetadata(romPath);
  if (!check(metadata.status, "Generated test ROM metadata could not be read")) {
    return 4;
  }

  {
    GameLibraryDatabase database{databasePath};
    const auto initialized = database.initialize();
    if (!check(initialized && database.isOpen() &&
        !database.recoveredCorruption() &&
        database.path() == databasePath,
        "New game-library database did not initialize")) {
      return 5;
    }
    const auto added = database.addDirectory(gamesPath, false);
    if (!check(added.status && added.directory.id > 0 &&
        added.directory.path == std::filesystem::canonical(gamesPath) &&
        !added.directory.recursive,
        "Game-library directory was not persisted")) {
      return 6;
    }
    if (!check(database.addDirectory(gamesPath, true).status.error ==
          GameLibraryError::directoryOverlap &&
        database.addDirectory(nestedPath, true).status.error ==
          GameLibraryError::directoryOverlap &&
        database.addDirectory(gamesPath.parent_path(), true).status.error ==
          GameLibraryError::directoryOverlap,
        "Duplicate or overlapping library roots were accepted")) {
      return 7;
    }
    const auto directories = database.directories();
    if (!check(directories.status && directories.directories.size() == 1U &&
        !directories.directories.front().recursive &&
        database.updateDirectory(added.directory.id, true),
        "Library directory listing/update failed")) {
      return 8;
    }
    GameLibraryStatus crossThreadStatus;
    std::thread crossThread{[&database, &crossThreadStatus] {
      crossThreadStatus = database.directories().status;
    }};
    crossThread.join();
    if (!check(crossThreadStatus.error == GameLibraryError::wrongThread,
        "Cross-thread database access was not rejected")) {
      return 9;
    }

    const auto scan = database.beginDirectoryScan(added.directory.id);
    const std::array records{
      LibraryScanRecord{
        .metadata = metadata.metadata,
        .lastModifiedEpochMilliseconds = 1'234'567,
      }};
    if (!check(scan.status && scan.generation > 0 &&
        database.applyScanBatch(added.directory.id, scan.generation, records),
        "Library scan transaction could not stage metadata")) {
      return 10;
    }
    std::vector<LibraryScanRecord> oversized(
      GameLibraryDatabase::maximumScanBatchSize + 1U, records.front());
    if (!check(database.applyScanBatch(
          added.directory.id, scan.generation, oversized).error ==
            GameLibraryError::invalidRecord,
        "An oversized scan batch was accepted")) {
      return 11;
    }
    const auto finished = database.finishDirectoryScan(
      added.directory.id, scan.generation);
    if (!check(finished.status && finished.removedGames == 0U,
        "Library scan transaction did not commit")) {
      return 12;
    }
    auto games = database.games();
    if (!check(games.status && games.games.size() == 1U &&
        games.games.front().metadata.path == romPath &&
        games.games.front().metadata.system == GameSystem::genesis &&
        games.games.front().metadata.sha256 == metadata.metadata.sha256 &&
        games.games.front().lastModifiedEpochMilliseconds == 1'234'567,
        "Indexed metadata did not round-trip through SQLite")) {
      return 13;
    }
    const auto gameId = games.games.front().id;
    const auto enrichment = onlineRecord(metadata.metadata.sha256);
    if (!check(database.setFavorite(gameId, true) &&
        database.setArtworkPath(gameId, artworkPath) &&
        database.setOnlineMetadata(gameId, enrichment, artworkPath) &&
        database.recordLaunch(gameId, 9'876'543),
        "Library favorite/artwork/metadata/launch state could not be updated")) {
      return 14;
    }

    const auto rescan = database.beginDirectoryScan(added.directory.id);
    if (!check(rescan.status && database.applyScanBatch(
          added.directory.id, rescan.generation, records) &&
        database.finishDirectoryScan(added.directory.id, rescan.generation).status,
        "Library rescan failed")) {
      return 15;
    }
    games = database.games();
    if (!check(games.status && games.games.front().favorite &&
        games.games.front().playCount == 1U &&
        games.games.front().lastPlayedEpochMilliseconds == 9'876'543 &&
        games.games.front().artworkPath ==
          std::filesystem::canonical(artworkPath) &&
        !games.games.front().artworkManaged &&
        games.games.front().onlineMetadata == enrichment &&
        games.games.front().displayTitle() == "Enriched Fixture Title",
        "A rescan discarded user-owned library state")) {
      return 16;
    }
    if (!check(database.clearOnlineMetadata(gameId),
        "Online metadata could not be cleared") ||
        !check(database.games().games.front().artworkPath ==
          std::filesystem::canonical(artworkPath),
          "Clearing online metadata removed user-owned artwork")) {
      return 17;
    }
    const auto clearedArtwork = database.setArtworkPath(gameId, {});
    const auto afterArtworkClear = database.games();
    const auto invalidArtwork = database.setArtworkPath(
      gameId, gamesPath / "missing.png");
    if (!check(clearedArtwork,
        "Local artwork path could not be cleared") ||
        !check(afterArtworkClear.status &&
          afterArtworkClear.games.front().artworkPath.empty(),
          "Cleared local artwork path remained in the database") ||
        !check(invalidArtwork.error == GameLibraryError::invalidPath,
          "Missing local artwork path was accepted")) {
      return 18;
    }
    if (!check(database.setOnlineMetadata(gameId, enrichment, artworkPath),
        "Managed online artwork could not be attached") ||
        !check(database.games().games.front().artworkManaged,
          "Online artwork was not marked as cache-managed") ||
        !check(database.clearOnlineMetadata(gameId) &&
          database.games().games.front().artworkPath.empty(),
          "Clearing online metadata did not detach cache-managed artwork")) {
      return 19;
    }
    auto changedRecords = records;
    changedRecords.front().metadata.sha256 = std::string(64U, 'b');
    const auto changedScan = database.beginDirectoryScan(added.directory.id);
    if (!check(changedScan.status && database.applyScanBatch(
          added.directory.id, changedScan.generation, changedRecords) &&
        database.finishDirectoryScan(
          added.directory.id, changedScan.generation).status,
        "Changed-content library rescan failed")) {
      return 20;
    }
    const auto changedGame = database.games();
    if (!check(changedGame.status && !changedGame.games.front().onlineMetadata &&
        changedGame.games.front().artworkPath.empty() &&
        !changedGame.games.front().artworkManaged,
        "Changed game content retained stale metadata or managed artwork")) {
      return 21;
    }

    const auto cancelled = database.beginDirectoryScan(added.directory.id);
    if (!check(cancelled.status && database.applyScanBatch(
          added.directory.id, cancelled.generation, records) &&
        database.cancelDirectoryScan() && database.games().games.size() == 1U,
        "Cancelled library scan did not roll back")) {
      return 22;
    }
    const auto emptyScan = database.beginDirectoryScan(added.directory.id);
    const auto emptied = database.finishDirectoryScan(
      added.directory.id, emptyScan.generation);
    if (!check(emptyScan.status && emptied.status && emptied.removedGames == 1U &&
        database.games().games.empty(),
        "Stale library entries were not removed transactionally")) {
      return 23;
    }
    if (!check(database.removeDirectory(added.directory.id) &&
        database.directories().directories.empty(),
        "Library directory removal did not cascade cleanly")) {
      return 24;
    }
  }

  constexpr std::string_view corruptText{"this is not a sqlite database"};
  if (!check(writeText(databasePath, corruptText),
      "Corrupt database fixture could not be staged")) {
    return 25;
  }
  std::filesystem::path recoveryPath;
  {
    GameLibraryDatabase recovered{databasePath};
    if (!check(recovered.initialize() && recovered.recoveredCorruption() &&
        !recovered.recoveryBackupPath().empty() &&
        std::filesystem::is_regular_file(recovered.recoveryBackupPath()) &&
        recovered.directories().directories.empty(),
        "Corrupt game-library database was not preserved and rebuilt")) {
      return 26;
    }
    recoveryPath = recovered.recoveryBackupPath();
  }
  if (!check(std::filesystem::file_size(recoveryPath) == corruptText.size(),
      "Recovered corruption backup did not preserve the original bytes") ||
      !check(executeSql(databasePath,
        QStringLiteral("DROP TABLE library_games")),
        "Logical schema corruption could not be staged")) {
    return 27;
  }
  {
    GameLibraryDatabase logicalCorruption{databasePath};
    if (!check(logicalCorruption.initialize() &&
        logicalCorruption.recoveredCorruption() &&
        std::filesystem::is_regular_file(
          logicalCorruption.recoveryBackupPath()) &&
        logicalCorruption.games().status,
        "A structurally incomplete database was not preserved and rebuilt")) {
      return 28;
    }
  }
  if (!check(setSchemaVersion(databasePath, 999U),
      "Future schema fixture could not be staged")) {
    return 29;
  }
  {
    GameLibraryDatabase future{databasePath};
    if (!check(future.initialize().error == GameLibraryError::unsupportedSchema &&
        !future.recoveredCorruption(),
        "A future database schema was accepted or destroyed")) {
      return 30;
    }
  }
  const auto legacyPath = pathIn(temporary, "legacy/game-library.sqlite3");
  if (!check(createLegacyVersionOneDatabase(legacyPath),
      "Version-one library database fixture could not be created")) {
    return 31;
  }
  {
    GameLibraryDatabase migrated{legacyPath};
    if (!check(migrated.initialize() && migrated.games().status &&
        executeSql(legacyPath, QStringLiteral(
          "SELECT online_metadata_json, artwork_managed FROM library_games LIMIT 0")),
        "Version-one library database was not migrated to schema version two")) {
      return 32;
    }
  }

  return 0;
}
