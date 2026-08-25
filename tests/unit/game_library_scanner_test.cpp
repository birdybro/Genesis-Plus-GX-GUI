#include "genplusgx/library/game_library_database.h"
#include "genplusgx/library/game_library_scanner.h"

#include "synthetic_rom.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>

namespace {

using namespace std::chrono_literals;

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

std::optional<genplusgx::library::GameLibraryScanEvent> waitForResult(
  genplusgx::library::GameLibraryScanner& scanner,
  std::uint64_t operationId)
{
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = scanner.waitForEvent(100ms);
    if (event && event->operationId == operationId &&
        (event->type ==
           genplusgx::library::GameLibraryScanEventType::scanCompleted ||
         event->type ==
           genplusgx::library::GameLibraryScanEventType::scanFailed)) {
      return event;
    }
  }
  return std::nullopt;
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
  const auto databasePath = pathIn(temporary, "library/index.sqlite3");
  const auto root = pathIn(temporary, "games");
  const auto nested = root / "nested";
  std::error_code error;
  std::filesystem::create_directories(nested, error);
  if (!check(!error, "Scanner fixture directories could not be created")) {
    return 2;
  }
  const auto topGame = root / "top.md";
  const auto nestedGame = nested / "nested.sg";
  const auto ignored = root / "notes.txt";
  const std::array<std::uint8_t, 4> sgBytes{1U, 2U, 3U, 4U};
  if (!check(writeBytes(topGame, genplusgx::test::makeGenesisRamMarkerRom()) &&
      writeBytes(nestedGame, sgBytes) && writeText(ignored, "not a game"),
      "Scanner fixtures could not be written")) {
    return 3;
  }

  std::int64_t directoryId = 0;
  {
    GameLibraryDatabase database{databasePath};
    const auto initialized = database.initialize();
    const auto directory = database.addDirectory(root, false);
    if (!check(initialized && directory.status,
        "Scanner database could not be configured")) {
      return 4;
    }
    directoryId = directory.directory.id;
  }

  GameLibraryScanner scanner{databasePath};
  if (!check(scanner.start(), "Game-library scanner did not start")) {
    return 5;
  }
  const auto started = scanner.waitForEvent(2s);
  if (!check(started && started->type ==
        GameLibraryScanEventType::serviceStarted &&
      !started->databaseRecovered,
      "Game-library scanner start event was not published")) {
    return 6;
  }
  if (!check(!scanner.requestScan(0U, directoryId) &&
      !scanner.requestScan(1U, 0),
      "Invalid scan request was accepted")) {
    return 7;
  }

  if (!check(scanner.requestScan(10U, directoryId),
      "Non-recursive scan request was rejected")) {
    return 8;
  }
  const auto first = waitForResult(scanner, 10U);
  if (!check(first && first->succeeded() &&
      first->summary.visitedFiles == 2U &&
      first->summary.supportedFiles == 1U &&
      first->summary.indexedGames == 1U,
      "Non-recursive library scan summary was incorrect")) {
    return 9;
  }
  {
    GameLibraryDatabase database{databasePath};
    if (!check(database.initialize() && database.games().games.size() == 1U &&
        database.games().games.front().metadata.path == topGame &&
        database.updateDirectory(directoryId, true),
        "Non-recursive scan result or recursive update was incorrect")) {
      return 10;
    }
  }

  if (!check(scanner.requestScan(11U, directoryId),
      "Recursive scan request was rejected")) {
    return 11;
  }
  const auto second = waitForResult(scanner, 11U);
  if (!check(second && second->succeeded() &&
      second->summary.visitedFiles == 3U &&
      second->summary.supportedFiles == 2U &&
      second->summary.indexedGames == 2U,
      "Recursive library scan summary was incorrect")) {
    return 12;
  }
  {
    GameLibraryDatabase database{databasePath};
    const auto initialized = database.initialize();
    const auto games = database.games();
    if (!check(initialized && games.status && games.games.size() == 2U,
        "Recursive scan did not index both generated games")) {
      return 13;
    }
  }

  if (!check(std::filesystem::remove(topGame, error) && !error,
      "Stale scanner fixture could not be removed") ||
      !check(scanner.requestScan(12U, directoryId),
        "Stale-entry scan request was rejected")) {
    return 14;
  }
  const auto third = waitForResult(scanner, 12U);
  if (!check(third && third->succeeded() &&
      third->summary.removedGames == 1U,
      "Stale library entry was not removed")) {
    return 15;
  }
  {
    GameLibraryDatabase database{databasePath};
    const auto initialized = database.initialize();
    const auto games = database.games();
    if (!check(initialized && games.status && games.games.size() == 1U &&
        games.games.front().metadata.system == GameSystem::sg1000 &&
        games.games.front().metadata.path == nestedGame,
        "Final scanner database contents were incorrect")) {
      return 16;
    }
  }

  const auto movedRoot = pathIn(temporary, "games-temporarily-missing");
  std::filesystem::rename(root, movedRoot, error);
  if (!check(!error && scanner.requestScan(13U, directoryId),
      "Missing-directory scan fixture could not be staged")) {
    return 17;
  }
  const auto missing = waitForResult(scanner, 13U);
  if (!check(missing && missing->type == GameLibraryScanEventType::scanFailed &&
      missing->databaseStatus.error == GameLibraryError::directoryNotFound,
      "Missing library directory did not fail safely")) {
    return 18;
  }
  {
    GameLibraryDatabase database{databasePath};
    if (!check(database.initialize() && database.games().games.size() == 1U,
        "Failed scan damaged the previous complete library index")) {
      return 19;
    }
  }
  std::filesystem::rename(movedRoot, root, error);
  if (!check(!error, "Scanner fixture directory could not be restored")) {
    return 20;
  }

  if (!check(scanner.stop(), "Game-library scanner did not stop")) {
    return 21;
  }
  const auto stopped = scanner.waitForEvent(2s);
  if (!check(stopped && stopped->type ==
      GameLibraryScanEventType::serviceStopped,
      "Game-library scanner stop event was not published")) {
    return 22;
  }
  if (!check(scanner.start() && scanner.waitForEvent(2s).has_value() &&
      scanner.stop(),
      "Game-library scanner did not restart cleanly")) {
    return 23;
  }

  const auto corruptDatabasePath = pathIn(temporary, "corrupt/library.sqlite3");
  std::filesystem::create_directories(corruptDatabasePath.parent_path(), error);
  if (!check(!error && writeText(corruptDatabasePath, "not sqlite"),
      "Scanner recovery fixture could not be staged")) {
    return 24;
  }
  GameLibraryScanner recoveryScanner{corruptDatabasePath};
  if (!check(recoveryScanner.start(), "Recovery scanner did not start")) {
    return 25;
  }
  const auto recoveryStarted = recoveryScanner.waitForEvent(2s);
  if (!check(recoveryStarted && recoveryStarted->type ==
        GameLibraryScanEventType::serviceStarted &&
      recoveryStarted->databaseRecovered &&
      std::filesystem::is_regular_file(recoveryStarted->recoveryBackupPath),
      "Scanner did not report database recovery")) {
    return 26;
  }
  if (!check(recoveryScanner.stop(), "Recovery scanner did not stop")) {
    return 27;
  }

  return 0;
}
