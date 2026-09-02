#pragma once

#include "genplusgx/netplay/netplay_types.h"

#include <cstddef>
#include <memory>
#include <optional>

namespace genplusgx::netplay {

class NetplayBridge final {
public:
  explicit NetplayBridge(std::size_t capacity = 256U);
  ~NetplayBridge();

  NetplayBridge(const NetplayBridge&) = delete;
  NetplayBridge& operator=(const NetplayBridge&) = delete;
  NetplayBridge(NetplayBridge&&) = delete;
  NetplayBridge& operator=(NetplayBridge&&) = delete;

  [[nodiscard]] NetplayBridgeStatus submitOutgoing(NetplayInputFrame frame);
  [[nodiscard]] std::optional<NetplayInputFrame> pollOutgoing();
  void clear() noexcept;
  [[nodiscard]] NetplayBridgeMetrics metrics() const noexcept;

private:
  class Private;
  std::unique_ptr<Private> private_;
};

} // namespace genplusgx::netplay
