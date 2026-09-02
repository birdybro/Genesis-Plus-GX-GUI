#include "genplusgx/achievements/achievement_bridge.h"
#include "genplusgx/achievements/achievement_types.h"

#include <utility>

namespace genplusgx::achievements {

ServerBridge::ServerBridge(std::size_t capacity)
    : requests_(capacity), responses_(capacity)
{
}

bool ServerBridge::submitRequest(ServerRequest request)
{
  std::scoped_lock lock{mutex_};
  if (request.id == 0U || request.url.empty() ||
      request.url.size() > 8U * 1024U ||
      request.postData.size() > maximumServerBodyBytes ||
      request.contentType.size() > 1U * 1024U ||
      !requests_.tryPush(std::move(request))) {
    ++rejectedRequests_;
    return false;
  }
  return true;
}

std::optional<ServerRequest> ServerBridge::takeRequest()
{
  std::scoped_lock lock{mutex_};
  return requests_.pop();
}

bool ServerBridge::submitResponse(ServerResponse response)
{
  std::scoped_lock lock{mutex_};
  if (response.id == 0U || response.body.size() > maximumServerBodyBytes ||
      !responses_.tryPush(std::move(response))) {
    ++rejectedResponses_;
    return false;
  }
  return true;
}

std::optional<ServerResponse> ServerBridge::takeResponse()
{
  std::scoped_lock lock{mutex_};
  return responses_.pop();
}

void ServerBridge::clear()
{
  std::scoped_lock lock{mutex_};
  requests_.clear();
  responses_.clear();
}

BridgeMetrics ServerBridge::metrics() const
{
  std::scoped_lock lock{mutex_};
  return {
    .requestDepth = requests_.size(),
    .responseDepth = responses_.size(),
    .capacity = requests_.capacity(),
    .rejectedRequests = rejectedRequests_,
    .rejectedResponses = rejectedResponses_,
  };
}

const char* connectionStateName(ConnectionState state) noexcept
{
  switch (state) {
    case ConnectionState::disabled: return "disabled";
    case ConnectionState::signedOut: return "signed out";
    case ConnectionState::signingIn: return "signing in";
    case ConnectionState::signedIn: return "signed in";
    case ConnectionState::loadingGame: return "loading game";
    case ConnectionState::active: return "active";
    case ConnectionState::offline: return "offline";
    case ConnectionState::error: return "error";
  }
  return "unknown";
}

} // namespace genplusgx::achievements
