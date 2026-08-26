#include "genplusgx/timing/frame_rate_sampler.h"

#include <chrono>

namespace genplusgx {

FrameRateSampler::FrameRateSampler(std::chrono::milliseconds minimumInterval)
  : minimumInterval_{minimumInterval > std::chrono::milliseconds::zero()
      ? minimumInterval : std::chrono::milliseconds{1}}
{
}

std::optional<double> FrameRateSampler::observe(
  std::uint64_t completedFrames,
  bool running,
  TimePoint now) noexcept
{
  if (!running) {
    const bool stopped = running_;
    previousFrames_ = completedFrames;
    previousTime_ = now;
    running_ = false;
    initialized_ = true;
    return stopped ? std::optional<double>{0.0} : std::nullopt;
  }

  if (!initialized_ || !running_ || now < previousTime_ ||
      completedFrames < previousFrames_) {
    previousFrames_ = completedFrames;
    previousTime_ = now;
    running_ = true;
    initialized_ = true;
    return std::nullopt;
  }

  const auto elapsed = now - previousTime_;
  if (elapsed < minimumInterval_) {
    return std::nullopt;
  }
  const auto elapsedSeconds = std::chrono::duration<double>{elapsed}.count();
  const auto frameDelta = completedFrames - previousFrames_;
  previousFrames_ = completedFrames;
  previousTime_ = now;
  return static_cast<double>(frameDelta) / elapsedSeconds;
}

void FrameRateSampler::reset() noexcept
{
  previousTime_ = TimePoint{};
  previousFrames_ = 0U;
  running_ = false;
  initialized_ = false;
}

} // namespace genplusgx
