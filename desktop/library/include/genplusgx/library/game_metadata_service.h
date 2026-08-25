#pragma once

#include "genplusgx/library/game_metadata.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace genplusgx::library {

enum class GameMetadataServiceError : std::uint8_t {
  none,
  alreadyRunning,
  notRunning,
  invalidRequest,
  queueFull,
  threadFailure,
};

struct GameMetadataServiceStatus final {
  GameMetadataServiceError error{GameMetadataServiceError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == GameMetadataServiceError::none;
  }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

enum class GameMetadataEventType : std::uint8_t {
  serviceStarted,
  metadataReady,
  operationFailed,
  serviceStopped,
};

struct GameMetadataEvent final {
  GameMetadataEventType type{GameMetadataEventType::operationFailed};
  std::uint64_t operationId{0};
  std::filesystem::path path;
  GameMetadataStatus status;
  GameMetadata metadata;

  [[nodiscard]] bool succeeded() const noexcept
  {
    return type == GameMetadataEventType::metadataReady && status.ok();
  }
};

class GameMetadataService final {
public:
  explicit GameMetadataService(
    std::size_t commandCapacity = 8U,
    std::size_t eventCapacity = 8U);
  ~GameMetadataService();

  GameMetadataService(const GameMetadataService&) = delete;
  GameMetadataService& operator=(const GameMetadataService&) = delete;
  GameMetadataService(GameMetadataService&&) = delete;
  GameMetadataService& operator=(GameMetadataService&&) = delete;

  [[nodiscard]] GameMetadataServiceStatus start();
  [[nodiscard]] GameMetadataServiceStatus request(
    std::uint64_t operationId,
    std::filesystem::path path);
  [[nodiscard]] std::optional<GameMetadataEvent> pollEvent();
  [[nodiscard]] std::optional<GameMetadataEvent> waitForEvent(
    std::chrono::milliseconds timeout);
  [[nodiscard]] GameMetadataServiceStatus stop();

private:
  class Private;
  std::unique_ptr<Private> private_;
};

} // namespace genplusgx::library
