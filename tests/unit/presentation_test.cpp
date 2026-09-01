#include "genplusgx/video/presentation.h"

#include <cmath>
#include <iostream>

namespace {

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

} // namespace

int main()
{
  using namespace genplusgx::video;
  const PresentationConfiguration defaults;
  if (!check(validatePresentationConfiguration(defaults),
        "Default presentation settings are invalid") ||
      !check(requestedSwapInterval(PresentationSyncMode::disabled) == 0 &&
          requestedSwapInterval(PresentationSyncMode::synchronized) == 1 &&
          requestedSwapInterval(PresentationSyncMode::adaptive) == -1,
        "A synchronization mode mapped to the wrong swap interval")) {
    return 1;
  }
  auto invalid = defaults;
  invalid.sync = static_cast<PresentationSyncMode>(99);
  if (!check(!validatePresentationConfiguration(invalid),
        "An invalid synchronization mode was accepted")) {
    return 2;
  }
  invalid = defaults;
  invalid.buffering = static_cast<PresentationBufferingMode>(99);
  if (!check(!validatePresentationConfiguration(invalid),
        "An invalid buffering mode was accepted")) {
    return 3;
  }

  PresentationTelemetry telemetry;
  telemetry.frameReceived(1U);
  telemetry.frameReceived(2U);
  telemetry.frameRendered(1U);
  if (!check(telemetry.snapshot().pendingFrames == 1U,
        "A stale render cleared a newer pending generation")) {
    return 4;
  }
  telemetry.frameReceived(3U);
  auto metrics = telemetry.snapshot();
  if (!check(metrics.receivedFrames == 3U &&
          metrics.coalescedFrames == 2U && metrics.pendingFrames == 1U &&
          metrics.maximumPendingFrames == 1U,
        "Newest-frame coalescing was not strictly bounded to one pending frame")) {
    return 5;
  }
  telemetry.frameRendered(3U);
  telemetry.frameRendered(3U);
  metrics = telemetry.snapshot();
  if (!check(metrics.renderedFrames == 2U &&
          metrics.duplicateRenders == 1U && metrics.pendingFrames == 0U,
        "Render submission telemetry did not distinguish duplicate repainting")) {
    return 6;
  }

  constexpr std::uint64_t interval = 16'667U;
  telemetry.frameSwapped(3U, 1'000'000U);
  for (std::uint64_t generation = 4U; generation <= 63U; ++generation) {
    telemetry.frameReceived(generation);
    telemetry.frameRendered(generation);
    telemetry.frameSwapped(
      generation, 1'000'000U + (generation - 3U) * interval);
  }
  metrics = telemetry.snapshot();
  if (!check(metrics.swappedFrames == 61U &&
          metrics.averageSwapIntervalMicroseconds == interval &&
          metrics.maximumSwapIntervalMicroseconds == interval &&
          std::abs(metrics.measuredFramesPerSecond -
            (1'000'000.0 / static_cast<double>(interval))) < 0.001,
        "Deterministic presentation cadence was measured incorrectly")) {
    return 7;
  }

  // Duplicate and backward timestamps must not underflow or inflate cadence.
  telemetry.frameSwapped(63U, 100U);
  telemetry.frameReceived(64U);
  telemetry.frameRendered(64U);
  telemetry.frameSwapped(64U, 99U);
  metrics = telemetry.snapshot();
  if (!check(metrics.swappedFrames == 62U &&
          metrics.averageSwapIntervalMicroseconds == interval &&
          metrics.maximumPendingFrames == 1U,
        "A duplicate or backward timestamp corrupted bounded telemetry")) {
    return 8;
  }

  telemetry.frameReceived(65U);
  telemetry.cancelPending();
  if (!check(telemetry.snapshot().pendingFrames == 0U,
        "Cancelling a pending frame left latent queue state")) {
    return 9;
  }
  telemetry.reset();
  metrics = telemetry.snapshot();
  return check(metrics.receivedFrames == 0U && metrics.pendingFrames == 0U &&
      metrics.measuredFramesPerSecond == 0.0,
    "Presentation telemetry did not reset deterministically")
    ? 0 : 10;
}
