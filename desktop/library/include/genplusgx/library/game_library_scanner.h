#pragma once

#include "genplusgx/library/game_library_database.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace genplusgx::library {

enum class GameLibraryScannerError : std::uint8_t {
  none,
  alreadyRunning,
  notRunning,
  invalidRequest,
  queueFull,
  databaseFailure,
  filesystemFailure,
  scanLimitExceeded,
  cancelled,
  threadFailure,
};

struct GameLibraryScannerStatus final {
  GameLibraryScannerError error{GameLibraryScannerError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == GameLibraryScannerError::none;
  }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

enum class GameLibraryScanEventType : std::uint8_t {
  serviceStarted,
  scanStarted,
  scanProgress,
  scanCompleted,
  scanFailed,
  serviceStopped,
};

struct GameLibraryScanSummary final {
  std::size_t visitedFiles{0};
  std::size_t supportedFiles{0};
  std::size_t indexedGames{0};
  std::size_t skippedFiles{0};
  std::size_t removedGames{0};
};

struct GameLibraryScanEvent final {
  GameLibraryScanEventType type{GameLibraryScanEventType::scanFailed};
  std::uint64_t operationId{0};
  std::int64_t directoryId{0};
  std::filesystem::path directoryPath;
  GameLibraryScannerStatus status;
  GameLibraryStatus databaseStatus;
  GameLibraryScanSummary summary;
  bool databaseRecovered{false};
  std::filesystem::path recoveryBackupPath;

  [[nodiscard]] bool succeeded() const noexcept
  {
    return type == GameLibraryScanEventType::scanCompleted && status.ok();
  }
};

class GameLibraryScanner final {
public:
  static constexpr std::size_t maximumVisitedFilesPerScan = 100'000U;

  explicit GameLibraryScanner(
    std::filesystem::path databasePath,
    std::size_t commandCapacity = 8U,
    std::size_t eventCapacity = 32U);
  ~GameLibraryScanner();

  GameLibraryScanner(const GameLibraryScanner&) = delete;
  GameLibraryScanner& operator=(const GameLibraryScanner&) = delete;
  GameLibraryScanner(GameLibraryScanner&&) = delete;
  GameLibraryScanner& operator=(GameLibraryScanner&&) = delete;

  [[nodiscard]] GameLibraryScannerStatus start();
  [[nodiscard]] GameLibraryScannerStatus requestScan(
    std::uint64_t operationId,
    std::int64_t directoryId);
  [[nodiscard]] std::optional<GameLibraryScanEvent> pollEvent();
  [[nodiscard]] std::optional<GameLibraryScanEvent> waitForEvent(
    std::chrono::milliseconds timeout);
  [[nodiscard]] GameLibraryScannerStatus stop();

private:
  class Private;
  std::unique_ptr<Private> private_;
};

} // namespace genplusgx::library
