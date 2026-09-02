#include "genplusgx/netplay/netplay_bridge.h"

#include "genplusgx/bounded_queue.h"

#include <mutex>
#include <utility>

namespace genplusgx::netplay {

class NetplayBridge::Private final {
public:
  explicit Private(std::size_t capacity) : outgoing_(capacity) {}

  mutable std::mutex mutex_;
  BoundedQueue<NetplayInputFrame> outgoing_;
  std::uint64_t submittedFrames_{0U};
  std::uint64_t consumedFrames_{0U};
  std::uint64_t rejectedFrames_{0U};
};

NetplayBridge::NetplayBridge(std::size_t capacity)
  : private_(std::make_unique<Private>(capacity))
{
}

NetplayBridge::~NetplayBridge() = default;

NetplayBridgeStatus NetplayBridge::submitOutgoing(NetplayInputFrame frame)
{
  std::scoped_lock lock{private_->mutex_};
  if (!private_->outgoing_.tryPush(std::move(frame))) {
    ++private_->rejectedFrames_;
    return {
      .error = NetplayBridgeError::queueFull,
      .message = "The bounded netplay output queue is full.",
    };
  }
  ++private_->submittedFrames_;
  return {};
}

std::optional<NetplayInputFrame> NetplayBridge::pollOutgoing()
{
  std::scoped_lock lock{private_->mutex_};
  auto frame = private_->outgoing_.pop();
  if (frame) {
    ++private_->consumedFrames_;
  }
  return frame;
}

void NetplayBridge::clear() noexcept
{
  std::scoped_lock lock{private_->mutex_};
  private_->outgoing_.clear();
}

NetplayBridgeMetrics NetplayBridge::metrics() const noexcept
{
  std::scoped_lock lock{private_->mutex_};
  return {
    .outgoingDepth = private_->outgoing_.size(),
    .outgoingCapacity = private_->outgoing_.capacity(),
    .submittedFrames = private_->submittedFrames_,
    .consumedFrames = private_->consumedFrames_,
    .rejectedFrames = private_->rejectedFrames_,
  };
}

} // namespace genplusgx::netplay
