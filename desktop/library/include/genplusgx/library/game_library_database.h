#pragma once

#include "genplusgx/library/game_metadata.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace genplusgx::library {

enum class GameLibraryError : std::uint8_t {
  none,
  invalidPath,
  directoryNotFound,
  directoryOverlap,
  databaseUnavailable,
  databaseCorrupt,
  recoveryFailed,
  unsupportedSchema,
  queryFailed,
  transactionFailed,
  wrongThread,
  invalidRecord,
  scanNotActive,
};

struct GameLibraryStatus final {
  GameLibraryError error{GameLibraryError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == GameLibraryError::none;
  }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

struct LibraryDirectory final {
  std::int64_t id{0};
  std::filesystem::path path;
  bool recursive{true};
};

struct LibraryDirectoryResult final {
  GameLibraryStatus status;
  LibraryDirectory directory;
};

struct LibraryDirectoriesResult final {
  GameLibraryStatus status;
  std::vector<LibraryDirectory> directories;
};

struct LibraryGame final {
  std::int64_t id{0};
  std::int64_t directoryId{0};
  GameMetadata metadata;
  std::int64_t lastModifiedEpochMilliseconds{0};
  bool favorite{false};
  std::optional<std::int64_t> lastPlayedEpochMilliseconds;
  std::uint64_t playCount{0};
  std::filesystem::path artworkPath;
};

struct LibraryGamesResult final {
  GameLibraryStatus status;
  std::vector<LibraryGame> games;
};

struct LibraryScanRecord final {
  GameMetadata metadata;
  std::int64_t lastModifiedEpochMilliseconds{0};
};

struct LibraryScanStartResult final {
  GameLibraryStatus status;
  std::int64_t generation{0};
};

struct LibraryScanFinishResult final {
  GameLibraryStatus status;
  std::size_t removedGames{0};
};

class GameLibraryDatabase final {
public:
  static constexpr std::uint32_t currentSchemaVersion = 1U;
  static constexpr std::size_t maximumConfiguredDirectories = 256U;
  static constexpr std::size_t maximumScanBatchSize = 128U;

  explicit GameLibraryDatabase(std::filesystem::path databasePath);
  ~GameLibraryDatabase();

  GameLibraryDatabase(const GameLibraryDatabase&) = delete;
  GameLibraryDatabase& operator=(const GameLibraryDatabase&) = delete;
  GameLibraryDatabase(GameLibraryDatabase&&) = delete;
  GameLibraryDatabase& operator=(GameLibraryDatabase&&) = delete;

  [[nodiscard]] GameLibraryStatus initialize();
  [[nodiscard]] bool isOpen() const noexcept;
  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] bool recoveredCorruption() const noexcept;
  [[nodiscard]] const std::filesystem::path& recoveryBackupPath() const noexcept;

  [[nodiscard]] LibraryDirectoryResult addDirectory(
    const std::filesystem::path& path,
    bool recursive = true);
  [[nodiscard]] GameLibraryStatus updateDirectory(
    std::int64_t directoryId,
    bool recursive);
  [[nodiscard]] GameLibraryStatus removeDirectory(std::int64_t directoryId);
  [[nodiscard]] LibraryDirectoriesResult directories() const;
  [[nodiscard]] LibraryGamesResult games() const;
  [[nodiscard]] GameLibraryStatus setFavorite(std::int64_t gameId, bool favorite);
  [[nodiscard]] GameLibraryStatus setArtworkPath(
    std::int64_t gameId,
    const std::filesystem::path& artworkPath);
  [[nodiscard]] GameLibraryStatus recordLaunch(
    std::int64_t gameId,
    std::int64_t epochMilliseconds);

  [[nodiscard]] LibraryScanStartResult beginDirectoryScan(
    std::int64_t directoryId);
  [[nodiscard]] GameLibraryStatus applyScanBatch(
    std::int64_t directoryId,
    std::int64_t generation,
    std::span<const LibraryScanRecord> records);
  [[nodiscard]] LibraryScanFinishResult finishDirectoryScan(
    std::int64_t directoryId,
    std::int64_t generation);
  [[nodiscard]] GameLibraryStatus cancelDirectoryScan();

private:
  class Private;
  std::unique_ptr<Private> private_;
};

} // namespace genplusgx::library
