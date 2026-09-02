#pragma once

#include "genplusgx/library/online_metadata_client.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace genplusgx::library {

enum class OnlineMetadataEventType : std::uint8_t {
  serviceStarted,
  lookupCompleted,
  lookupFailed,
  serviceStopped,
};

struct OnlineMetadataEvent final {
  OnlineMetadataEventType type{OnlineMetadataEventType::lookupFailed};
  std::uint64_t operationId{0U};
  std::int64_t libraryGameId{0};
  OnlineMetadataLookupResult result;
};

using OnlineTransportFactory = std::function<std::unique_ptr<OnlineHttpTransport>(
  OnlineMetadataCancellation)>;

class OnlineMetadataService final {
public:
  explicit OnlineMetadataService(
    std::size_t commandCapacity = 4U,
    std::size_t eventCapacity = 8U,
    OnlineTransportFactory transportFactory = {});
  ~OnlineMetadataService();

  OnlineMetadataService(const OnlineMetadataService&) = delete;
  OnlineMetadataService& operator=(const OnlineMetadataService&) = delete;

  [[nodiscard]] OnlineMetadataStatus start();
  [[nodiscard]] OnlineMetadataStatus request(
    std::uint64_t operationId,
    std::int64_t libraryGameId,
    OnlineMetadataSettings settings,
    GameMetadata game,
    std::filesystem::path cacheDirectory);
  [[nodiscard]] std::optional<OnlineMetadataEvent> pollEvent();
  [[nodiscard]] std::optional<OnlineMetadataEvent> waitForEvent(
    std::chrono::milliseconds timeout);
  [[nodiscard]] OnlineMetadataStatus stop();

private:
  class Private;
  std::unique_ptr<Private> private_;
};

} // namespace genplusgx::library
