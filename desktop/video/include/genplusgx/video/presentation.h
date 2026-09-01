#pragma once

#include <cstddef>
#include <cstdint>

namespace genplusgx::video {

enum class PresentationSyncMode : std::uint8_t {
  disabled,
  synchronized,
  adaptive,
};

enum class PresentationBufferingMode : std::uint8_t {
  doubleBuffer,
  tripleBuffer,
};

struct PresentationConfiguration final {
  PresentationSyncMode sync{PresentationSyncMode::synchronized};
  PresentationBufferingMode buffering{
    PresentationBufferingMode::doubleBuffer};

  [[nodiscard]] bool operator==(
    const PresentationConfiguration&) const = default;
};

[[nodiscard]] bool validatePresentationConfiguration(
  const PresentationConfiguration& configuration) noexcept;
[[nodiscard]] int requestedSwapInterval(
  PresentationSyncMode mode) noexcept;

struct PresentationTelemetrySnapshot final {
  std::uint64_t receivedFrames{0U};
  std::uint64_t renderedFrames{0U};
  std::uint64_t swappedFrames{0U};
  std::uint64_t coalescedFrames{0U};
  std::uint64_t duplicateRenders{0U};
  std::size_t pendingFrames{0U};
  std::size_t maximumPendingFrames{0U};
  double measuredFramesPerSecond{0.0};
  std::uint64_t averageSwapIntervalMicroseconds{0U};
  std::uint64_t maximumSwapIntervalMicroseconds{0U};
};

// GUI-thread telemetry for the newest-frame-only presentation path. Timestamps
// are supplied by the caller so cadence behavior remains deterministic in tests.
class PresentationTelemetry final {
public:
  void frameReceived(std::uint64_t generation) noexcept;
  void frameRendered(std::uint64_t generation) noexcept;
  void frameSwapped(
    std::uint64_t generation,
    std::uint64_t timestampMicroseconds) noexcept;
  void cancelPending() noexcept;
  void reset() noexcept;

  [[nodiscard]] PresentationTelemetrySnapshot snapshot() const noexcept;

private:
  std::uint64_t latestReceivedGeneration_{0U};
  std::uint64_t pendingGeneration_{0U};
  std::uint64_t lastRenderedGeneration_{0U};
  std::uint64_t lastSwappedGeneration_{0U};
  std::uint64_t lastSwapTimestampMicroseconds_{0U};
  std::uint64_t measuredIntervalMicroseconds_{0U};
  std::uint64_t measuredIntervalCount_{0U};
  PresentationTelemetrySnapshot snapshot_;
};

} // namespace genplusgx::video
