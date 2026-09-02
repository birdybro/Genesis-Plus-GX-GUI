#include "genplusgx/netplay/netplay_timeline.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <utility>

namespace genplusgx::netplay {
namespace {

bool lowercaseHex(const std::string& value, std::size_t size) noexcept
{
  return value.size() == size &&
    std::ranges::all_of(value, [](unsigned char character) {
      return std::isdigit(character) != 0 ||
        (character >= static_cast<unsigned char>('a') &&
         character <= static_cast<unsigned char>('f'));
    });
}

std::uint64_t saturatedAdd(
  std::uint64_t value,
  std::uint64_t increment) noexcept
{
  return increment > std::numeric_limits<std::uint64_t>::max() - value
    ? std::numeric_limits<std::uint64_t>::max() : value + increment;
}

} // namespace

bool NetplaySessionDescriptor::valid() const noexcept
{
  return lowercaseHex(gameSha256, 64U) &&
    lowercaseHex(settingsSha256, 64U) &&
    !coreVersion.empty() && coreVersion.size() <= 64U;
}

bool NetplayConfiguration::valid() const noexcept
{
  return localPlayer < InputSnapshot::maximumPlayers &&
    remotePlayer < InputSnapshot::maximumPlayers &&
    localPlayer != remotePlayer &&
    inputDelayFrames <= maximumInputDelayFrames &&
    rollbackFrames > 0U && rollbackFrames <= maximumRollbackFrames;
}

NetplayTimelineStatus NetplayTimeline::configure(
  NetplayConfiguration configuration,
  std::uint64_t initialFrame)
{
  if (!configuration.valid()) {
    return {
      .error = NetplayTimelineError::invalidConfiguration,
      .message = "The netplay player assignment, delay, or rollback window is invalid.",
    };
  }
  reset();
  active_ = true;
  configuration_ = configuration;
  initialFrame_ = initialFrame;
  return {};
}

void NetplayTimeline::reset() noexcept
{
  active_ = false;
  localFrames_.clear();
  remoteFrames_.clear();
  usedFrames_.clear();
  rollbackRequest_.reset();
  predictedFrames_ = 0U;
  rollbackRequests_ = 0U;
  rejectedFrames_ = 0U;
}

NetplayTimelineStatus NetplayTimeline::submitRemote(
  NetplayInputFrame frame,
  std::uint64_t currentFrame)
{
  if (!active_) {
    return {
      .error = NetplayTimelineError::notConfigured,
      .message = "No netplay input timeline is active.",
    };
  }
  const auto windowStart = currentFrame > configuration_.rollbackFrames
    ? currentFrame - configuration_.rollbackFrames : 0U;
  const auto oldest = std::max(initialFrame_, windowStart);
  if (frame.frameNumber < oldest) {
    ++rejectedFrames_;
    return {
      .error = NetplayTimelineError::frameTooOld,
      .message = "A remote input arrived outside the rollback window.",
    };
  }
  const auto maximumAhead = saturatedAdd(currentFrame,
    maximumInputDelayFrames + maximumRollbackFrames + 8U);
  if (frame.frameNumber > maximumAhead) {
    ++rejectedFrames_;
    return {
      .error = NetplayTimelineError::frameTooFarAhead,
      .message = "A remote input frame is implausibly far ahead of emulation.",
    };
  }
  if (const auto existing = remoteFrames_.find(frame.frameNumber);
      existing != remoteFrames_.end()) {
    if (existing->second == frame.state) {
      return {};
    }
    ++rejectedFrames_;
    return {
      .error = NetplayTimelineError::duplicateConflict,
      .message = "A peer sent conflicting input for the same frame.",
    };
  }
  remoteFrames_.emplace(frame.frameNumber, frame.state);
  if (const auto used = usedFrames_.find(frame.frameNumber);
      used != usedFrames_.end() && used->second.remote != frame.state) {
    rollbackRequest_ = rollbackRequest_
      ? std::min(*rollbackRequest_, frame.frameNumber) : frame.frameNumber;
    ++rollbackRequests_;
  }
  return {};
}

PreparedNetplayInput NetplayTimeline::prepareFrame(
  std::uint64_t frameNumber,
  const InputSnapshot& latestLocal)
{
  const auto target = saturatedAdd(
    frameNumber, configuration_.inputDelayFrames);
  const auto localState = latestLocal.players[configuration_.localPlayer];
  localFrames_.try_emplace(target, localState);

  const auto local = localFor(frameNumber);
  const auto [remote, predicted] = remoteFor(frameNumber);
  usedFrames_.insert_or_assign(
    frameNumber, UsedFrame{local, remote, predicted});
  if (predicted) {
    ++predictedFrames_;
  }
  return {
    .combined = combine(frameNumber, local, remote),
    .outgoing = {.frameNumber = target, .state = localState},
    .remotePredicted = predicted,
  };
}

InputSnapshot NetplayTimeline::replayInput(std::uint64_t frameNumber)
{
  const auto local = localFor(frameNumber);
  const auto [remote, predicted] = remoteFor(frameNumber);
  usedFrames_.insert_or_assign(
    frameNumber, UsedFrame{local, remote, predicted});
  return combine(frameNumber, local, remote);
}

std::optional<std::uint64_t> NetplayTimeline::takeRollbackRequest() noexcept
{
  return std::exchange(rollbackRequest_, std::nullopt);
}

void NetplayTimeline::discardFrom(std::uint64_t frameNumber) noexcept
{
  usedFrames_.erase(usedFrames_.lower_bound(frameNumber), usedFrames_.end());
}

void NetplayTimeline::prune(std::uint64_t currentFrame) noexcept
{
  const auto retention = static_cast<std::uint64_t>(
    configuration_.rollbackFrames + configuration_.inputDelayFrames + 2U);
  const auto windowStart = currentFrame > retention
    ? currentFrame - retention : 0U;
  const auto oldest = std::max(initialFrame_, windowStart);
  localFrames_.erase(localFrames_.begin(), localFrames_.lower_bound(oldest));
  remoteFrames_.erase(remoteFrames_.begin(), remoteFrames_.lower_bound(oldest));
  usedFrames_.erase(usedFrames_.begin(), usedFrames_.lower_bound(oldest));
}

NetplayTimelineMetrics NetplayTimeline::metrics() const noexcept
{
  return {
    .active = active_,
    .storedLocalFrames = localFrames_.size(),
    .storedRemoteFrames = remoteFrames_.size(),
    .usedFrames = usedFrames_.size(),
    .predictedFrames = predictedFrames_,
    .rollbackRequests = rollbackRequests_,
    .rejectedFrames = rejectedFrames_,
  };
}

InputDeviceState NetplayTimeline::localFor(std::uint64_t frameNumber) const
{
  if (const auto found = localFrames_.find(frameNumber);
      found != localFrames_.end()) {
    return found->second;
  }
  return {};
}

std::pair<InputDeviceState, bool> NetplayTimeline::remoteFor(
  std::uint64_t frameNumber) const
{
  if (const auto found = remoteFrames_.find(frameNumber);
      found != remoteFrames_.end()) {
    return {found->second, false};
  }
  const auto previous = remoteFrames_.lower_bound(frameNumber);
  if (previous != remoteFrames_.begin()) {
    return {std::prev(previous)->second, true};
  }
  return {{}, true};
}

InputSnapshot NetplayTimeline::combine(
  std::uint64_t frameNumber,
  const InputDeviceState& local,
  const InputDeviceState& remote) const
{
  InputSnapshot combined;
  combined.sequence = saturatedAdd(frameNumber, 1U);
  combined.players[configuration_.localPlayer] = local;
  combined.players[configuration_.remotePlayer] = remote;
  return combined;
}

} // namespace genplusgx::netplay
