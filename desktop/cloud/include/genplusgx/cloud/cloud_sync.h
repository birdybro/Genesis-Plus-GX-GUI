#pragma once

#include "genplusgx/cloud/cloud_manifest.h"
#include "genplusgx/cloud/cloud_remote.h"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

namespace genplusgx::cloud {

[[nodiscard]] SyncResult synchronize(
  const ApplicationPaths& paths,
  const Settings& settings,
  RemoteStore& remote,
  const Cancellation& cancellation = {},
  std::chrono::system_clock::time_point timestamp =
    std::chrono::system_clock::now());

enum class EventType : std::uint8_t {
  serviceStarted,
  syncCompleted,
  syncFailed,
  serviceStopped,
};

struct Event final {
  EventType type{EventType::syncFailed};
  std::uint64_t operationId{0U};
  SyncResult result;
};

using RemoteFactory = std::function<std::unique_ptr<RemoteStore>(
  const Settings&, std::string, Cancellation)>;

class SyncService final {
public:
  explicit SyncService(RemoteFactory remoteFactory = {},
    std::size_t commandCapacity = 1U,
    std::size_t eventCapacity = 8U);
  ~SyncService();

  SyncService(const SyncService&) = delete;
  SyncService& operator=(const SyncService&) = delete;

  [[nodiscard]] Status start();
  [[nodiscard]] Status request(std::uint64_t operationId,
    ApplicationPaths paths, Settings settings, std::string password);
  [[nodiscard]] std::optional<Event> pollEvent();
  [[nodiscard]] std::optional<Event> waitForEvent(
    std::chrono::milliseconds timeout);
  [[nodiscard]] Status stop();

private:
  class Private;
  std::unique_ptr<Private> private_;
};

} // namespace genplusgx::cloud
