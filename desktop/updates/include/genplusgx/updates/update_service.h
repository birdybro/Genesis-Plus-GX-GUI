#pragma once

#include "genplusgx/updates/update_client.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace genplusgx::updates {

enum class EventType : std::uint8_t {
  serviceStarted,
  checkCompleted,
  checkFailed,
  downloadCompleted,
  downloadFailed,
  serviceStopped,
};

struct Event final {
  EventType type{EventType::checkFailed};
  std::uint64_t operationId{0U};
  CheckResult check;
  DownloadResult download;
};

using TransportFactory = std::function<std::unique_ptr<HttpTransport>(Cancellation)>;

class Service final {
public:
  explicit Service(std::size_t commandCapacity = 2U,
    std::size_t eventCapacity = 8U,
    TransportFactory transportFactory = {});
  ~Service();
  Service(const Service&) = delete;
  Service& operator=(const Service&) = delete;

  [[nodiscard]] Status start();
  [[nodiscard]] Status requestCheck(std::uint64_t operationId,
    Settings settings, std::string currentVersion, Trust trust = productionTrust());
  [[nodiscard]] Status requestDownload(std::uint64_t operationId, Asset asset,
    std::filesystem::path destinationDirectory,
    Trust trust = productionTrust());
  [[nodiscard]] std::optional<Event> pollEvent();
  [[nodiscard]] std::optional<Event> waitForEvent(std::chrono::milliseconds timeout);
  [[nodiscard]] Status stop();

private:
  class Private;
  std::unique_ptr<Private> private_;
};

} // namespace genplusgx::updates
