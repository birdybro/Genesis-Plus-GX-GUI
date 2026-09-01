#include "genplusgx/rewind_buffer.h"

#include <utility>

namespace genplusgx {

RewindBuffer::RewindBuffer(RewindConfiguration configuration)
  : configuration_(validateRewindConfiguration(configuration)
      ? configuration : RewindConfiguration{})
{
}

bool RewindBuffer::configure(RewindConfiguration configuration)
{
  if (!validateRewindConfiguration(configuration)) {
    return false;
  }
  if (configuration == configuration_) {
    return true;
  }
  configuration_ = configuration;
  clear();
  return true;
}

const RewindConfiguration& RewindBuffer::configuration() const noexcept
{
  return configuration_;
}

void RewindBuffer::clear() noexcept
{
  snapshots_.clear();
  payloadBytes_ = 0U;
}

bool RewindBuffer::capture(
  std::uint64_t frameNumber,
  std::vector<std::uint8_t> rawState)
{
  if (!configuration_.enabled || rawState.empty() ||
      rawState.size() > configuration_.memoryLimitBytes ||
      (!snapshots_.empty() && snapshots_.back().frameNumber >= frameNumber)) {
    return false;
  }
  payloadBytes_ += rawState.size();
  snapshots_.push_back({frameNumber, std::move(rawState)});
  trimToLimit();
  return true;
}

bool RewindBuffer::shouldCapture(std::uint64_t frameNumber) const noexcept
{
  return configuration_.enabled &&
    frameNumber % configuration_.captureIntervalFrames == 0U &&
    (snapshots_.empty() || snapshots_.back().frameNumber < frameNumber);
}

bool RewindBuffer::canRewind(std::uint64_t currentFrameNumber) const noexcept
{
  if (currentFrameNumber < 2U) {
    return false;
  }
  const auto latestUsableState = currentFrameNumber - 1U;
  for (auto iterator = snapshots_.rbegin(); iterator != snapshots_.rend(); ++iterator) {
    if (iterator->frameNumber < latestUsableState) {
      return true;
    }
  }
  return false;
}

std::optional<RewindSnapshot> RewindBuffer::takePrevious(
  std::uint64_t currentFrameNumber)
{
  while (!snapshots_.empty() &&
         snapshots_.back().frameNumber >= currentFrameNumber) {
    payloadBytes_ -= snapshots_.back().rawState.size();
    snapshots_.pop_back();
    ++discardedSnapshots_;
  }
  if (snapshots_.empty()) {
    return std::nullopt;
  }
  auto snapshot = std::move(snapshots_.back());
  payloadBytes_ -= snapshot.rawState.size();
  snapshots_.pop_back();
  return snapshot;
}

RewindBufferMetrics RewindBuffer::metrics() const noexcept
{
  return {
    .snapshotCount = snapshots_.size(),
    .payloadBytes = payloadBytes_,
    .memoryLimitBytes = configuration_.memoryLimitBytes,
    .discardedSnapshots = discardedSnapshots_,
  };
}

void RewindBuffer::trimToLimit() noexcept
{
  while (payloadBytes_ > configuration_.memoryLimitBytes && !snapshots_.empty()) {
    payloadBytes_ -= snapshots_.front().rawState.size();
    snapshots_.pop_front();
    ++discardedSnapshots_;
  }
}

} // namespace genplusgx
