#include "genplusgx/video/presentation.h"

#include <algorithm>
#include <limits>

namespace genplusgx::video {

bool validatePresentationConfiguration(
  const PresentationConfiguration& configuration) noexcept
{
  return static_cast<unsigned>(configuration.sync) <=
      static_cast<unsigned>(PresentationSyncMode::adaptive) &&
    static_cast<unsigned>(configuration.buffering) <=
      static_cast<unsigned>(PresentationBufferingMode::tripleBuffer);
}

int requestedSwapInterval(PresentationSyncMode mode) noexcept
{
  switch (mode) {
    case PresentationSyncMode::disabled:
      return 0;
    case PresentationSyncMode::synchronized:
      return 1;
    case PresentationSyncMode::adaptive:
      return -1;
  }
  return 1;
}

void PresentationTelemetry::frameReceived(std::uint64_t generation) noexcept
{
  if (generation == 0U || generation == latestReceivedGeneration_) {
    return;
  }
  if (pendingGeneration_ != 0U) {
    ++snapshot_.coalescedFrames;
  }
  latestReceivedGeneration_ = generation;
  pendingGeneration_ = generation;
  ++snapshot_.receivedFrames;
  snapshot_.pendingFrames = 1U;
  snapshot_.maximumPendingFrames = std::max(
    snapshot_.maximumPendingFrames, snapshot_.pendingFrames);
}

void PresentationTelemetry::frameRendered(std::uint64_t generation) noexcept
{
  if (generation == 0U) {
    return;
  }
  if (generation == lastRenderedGeneration_) {
    ++snapshot_.duplicateRenders;
    return;
  }
  lastRenderedGeneration_ = generation;
  ++snapshot_.renderedFrames;
  if (pendingGeneration_ != 0U && generation >= pendingGeneration_) {
    pendingGeneration_ = 0U;
    snapshot_.pendingFrames = 0U;
  }
}

void PresentationTelemetry::frameSwapped(
  std::uint64_t generation,
  std::uint64_t timestampMicroseconds) noexcept
{
  if (generation == 0U || generation == lastSwappedGeneration_) {
    return;
  }
  lastSwappedGeneration_ = generation;
  ++snapshot_.swappedFrames;
  if (lastSwapTimestampMicroseconds_ != 0U &&
      timestampMicroseconds > lastSwapTimestampMicroseconds_) {
    const auto interval = timestampMicroseconds - lastSwapTimestampMicroseconds_;
    if (measuredIntervalMicroseconds_ <=
        std::numeric_limits<std::uint64_t>::max() - interval) {
      measuredIntervalMicroseconds_ += interval;
      ++measuredIntervalCount_;
    }
    snapshot_.maximumSwapIntervalMicroseconds = std::max(
      snapshot_.maximumSwapIntervalMicroseconds, interval);
  }
  lastSwapTimestampMicroseconds_ = timestampMicroseconds;
  if (measuredIntervalCount_ != 0U && measuredIntervalMicroseconds_ != 0U) {
    snapshot_.averageSwapIntervalMicroseconds =
      measuredIntervalMicroseconds_ / measuredIntervalCount_;
    snapshot_.measuredFramesPerSecond =
      1'000'000.0 * static_cast<double>(measuredIntervalCount_) /
      static_cast<double>(measuredIntervalMicroseconds_);
  }
}

void PresentationTelemetry::cancelPending() noexcept
{
  pendingGeneration_ = 0U;
  snapshot_.pendingFrames = 0U;
}

void PresentationTelemetry::reset() noexcept
{
  *this = {};
}

PresentationTelemetrySnapshot PresentationTelemetry::snapshot() const noexcept
{
  return snapshot_;
}

} // namespace genplusgx::video
