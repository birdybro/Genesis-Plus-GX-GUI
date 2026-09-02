#pragma once

#include "genplusgx/netplay/netplay_types.h"

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>

namespace genplusgx::netplay {

enum class NetplayTimelineError {
  none,
  notConfigured,
  invalidConfiguration,
  duplicateConflict,
  frameTooOld,
  frameTooFarAhead,
};

struct NetplayTimelineStatus final {
  NetplayTimelineError error{NetplayTimelineError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == NetplayTimelineError::none;
  }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

struct PreparedNetplayInput final {
  InputSnapshot combined;
  NetplayInputFrame outgoing;
  bool remotePredicted{false};
};

struct NetplayTimelineMetrics final {
  bool active{false};
  std::size_t storedLocalFrames{0U};
  std::size_t storedRemoteFrames{0U};
  std::size_t usedFrames{0U};
  std::uint64_t predictedFrames{0U};
  std::uint64_t rollbackRequests{0U};
  std::uint64_t rejectedFrames{0U};
};

class NetplayTimeline final {
public:
  [[nodiscard]] NetplayTimelineStatus configure(
    NetplayConfiguration configuration,
    std::uint64_t initialFrame = 0U);
  void reset() noexcept;

  [[nodiscard]] NetplayTimelineStatus submitRemote(
    NetplayInputFrame frame,
    std::uint64_t currentFrame);
  [[nodiscard]] PreparedNetplayInput prepareFrame(
    std::uint64_t frameNumber,
    const InputSnapshot& latestLocal);
  [[nodiscard]] InputSnapshot replayInput(std::uint64_t frameNumber);
  [[nodiscard]] std::optional<std::uint64_t> takeRollbackRequest() noexcept;
  void discardFrom(std::uint64_t frameNumber) noexcept;
  void prune(std::uint64_t currentFrame) noexcept;

  [[nodiscard]] bool active() const noexcept { return active_; }
  [[nodiscard]] const NetplayConfiguration& configuration() const noexcept
  {
    return configuration_;
  }
  [[nodiscard]] NetplayTimelineMetrics metrics() const noexcept;

private:
  struct UsedFrame final {
    InputDeviceState local;
    InputDeviceState remote;
    bool remotePredicted{false};
  };

  [[nodiscard]] InputDeviceState localFor(std::uint64_t frameNumber) const;
  [[nodiscard]] std::pair<InputDeviceState, bool> remoteFor(
    std::uint64_t frameNumber) const;
  [[nodiscard]] InputSnapshot combine(
    std::uint64_t frameNumber,
    const InputDeviceState& local,
    const InputDeviceState& remote) const;

  bool active_{false};
  NetplayConfiguration configuration_;
  std::uint64_t initialFrame_{0U};
  std::map<std::uint64_t, InputDeviceState> localFrames_;
  std::map<std::uint64_t, InputDeviceState> remoteFrames_;
  std::map<std::uint64_t, UsedFrame> usedFrames_;
  std::optional<std::uint64_t> rollbackRequest_;
  std::uint64_t predictedFrames_{0U};
  std::uint64_t rollbackRequests_{0U};
  std::uint64_t rejectedFrames_{0U};
};

} // namespace genplusgx::netplay
