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
  const auto romBytes = genplusgx::test::makeGenesisSramWriterRom();
  if (!check(writeBytes(romPath, romBytes), "Test ROM could not be written")) {
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
    if (!check(database.setFavorite(gameId, true) &&
        database.recordLaunch(gameId, 9'876'543),
        "Library favorite/launch state could not be updated")) {
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
        games.games.front().lastPlayedEpochMilliseconds == 9'876'543,
        "A rescan discarded user-owned library state")) {
      return 16;
    }

    const auto cancelled = database.beginDirectoryScan(added.directory.id);
    if (!check(cancelled.status && database.applyScanBatch(
          added.directory.id, cancelled.generation, records) &&
        database.cancelDirectoryScan() && database.games().games.size() == 1U,
        "Cancelled library scan did not roll back")) {
      return 17;
    }
    const auto emptyScan = database.beginDirectoryScan(added.directory.id);
    const auto emptied = database.finishDirectoryScan(
      added.directory.id, emptyScan.generation);
    if (!check(emptyScan.status && emptied.status && emptied.removedGames == 1U &&
        database.games().games.empty(),
        "Stale library entries were not removed transactionally")) {
      return 18;
    }
    if (!check(database.removeDirectory(added.directory.id) &&
        database.directories().directories.empty(),
        "Library directory removal did not cascade cleanly")) {
      return 19;
    }
  }

  constexpr std::string_view corruptText{"this is not a sqlite database"};
  if (!check(writeText(databasePath, corruptText),
      "Corrupt database fixture could not be staged")) {
    return 20;
  }
  std::filesystem::path recoveryPath;
  {
    GameLibraryDatabase recovered{databasePath};
    if (!check(recovered.initialize() && recovered.recoveredCorruption() &&
        !recovered.recoveryBackupPath().empty() &&
        std::filesystem::is_regular_file(recovered.recoveryBackupPath()) &&
        recovered.directories().directories.empty(),
        "Corrupt game-library database was not preserved and rebuilt")) {
      return 21;
    }
    recoveryPath = recovered.recoveryBackupPath();
  }
  if (!check(std::filesystem::file_size(recoveryPath) == corruptText.size(),
      "Recovered corruption backup did not preserve the original bytes") ||
      !check(executeSql(databasePath,
        QStringLiteral("DROP TABLE library_games")),
        "Logical schema corruption could not be staged")) {
    return 22;
  }
  {
    GameLibraryDatabase logicalCorruption{databasePath};
    if (!check(logicalCorruption.initialize() &&
        logicalCorruption.recoveredCorruption() &&
        std::filesystem::is_regular_file(
          logicalCorruption.recoveryBackupPath()) &&
        logicalCorruption.games().status,
        "A structurally incomplete database was not preserved and rebuilt")) {
      return 23;
    }
  }
  if (!check(setSchemaVersion(databasePath, 999U),
      "Future schema fixture could not be staged")) {
    return 24;
  }
  {
    GameLibraryDatabase future{databasePath};
    if (!check(future.initialize().error == GameLibraryError::unsupportedSchema &&
        !future.recoveredCorruption(),
        "A future database schema was accepted or destroyed")) {
      return 25;
    }
  }

  return 0;
}
