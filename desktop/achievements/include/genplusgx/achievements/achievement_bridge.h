#pragma once

#include "genplusgx/bounded_queue.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace genplusgx::achievements {

inline constexpr std::size_t maximumServerBodyBytes = 4U * 1024U * 1024U;

struct ServerRequest final {
  std::uint64_t id{0U};
  std::string url;
  std::string postData;
  std::string contentType;
};

struct ServerResponse final {
  std::uint64_t id{0U};
  int httpStatusCode{-1};
  std::string body;
};

struct BridgeMetrics final {
  std::size_t requestDepth{0U};
  std::size_t responseDepth{0U};
  std::size_t capacity{0U};
  std::uint64_t rejectedRequests{0U};
  std::uint64_t rejectedResponses{0U};
};

class ServerBridge final {
public:
  explicit ServerBridge(std::size_t capacity = 32U);

  [[nodiscard]] bool submitRequest(ServerRequest request);
  [[nodiscard]] std::optional<ServerRequest> takeRequest();
  [[nodiscard]] bool submitResponse(ServerResponse response);
  [[nodiscard]] std::optional<ServerResponse> takeResponse();
  void clear();
  [[nodiscard]] BridgeMetrics metrics() const;

private:
  mutable std::mutex mutex_;
  BoundedQueue<ServerRequest> requests_;
  BoundedQueue<ServerResponse> responses_;
  std::uint64_t rejectedRequests_{0U};
  std::uint64_t rejectedResponses_{0U};
};

} // namespace genplusgx::achievements
